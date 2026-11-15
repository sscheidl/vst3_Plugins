// Copyright 2014 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Group of voices.
//
// Modified 2026 for vst3_Plugins (TAUREON): runs at the sample rate given to Init();
// without the string models and the "ominous voice".
// See UPSTREAM.md.

#include "elements/dsp/part.h"

#include "elements/resources.h"

namespace elements {

using namespace std;
using namespace stmlib;

/* static */
size_t Part::ReverbBufferSize(float sample_rate) {
  SampleRate rate;
  if (!rate.Init(sample_rate)) {
    return 0;
  }
  return Reverb::BufferSize(rate);
}

bool Part::Init(
    float sample_rate,
    uint16_t* reverb_buffer,
    size_t reverb_buffer_size) {
  if (!sample_rate_.Init(sample_rate)) {
    return false;
  }
  rng_.Seed(Rng::kDefaultSeed);
  meter_attack_ = sample_rate_.OnePole(0.05f);
  meter_decay_ = sample_rate_.OnePole(0.0005f);

  patch_.exciter_envelope_shape = 1.0f;
  patch_.exciter_bow_level = 0.0f;
  patch_.exciter_bow_timbre = 0.5f;
  patch_.exciter_blow_level = 0.0f;
  patch_.exciter_blow_meta = 0.5f;
  patch_.exciter_blow_timbre = 0.5f;
  patch_.exciter_strike_level = 0.8f;
  patch_.exciter_strike_meta = 0.5f;
  patch_.exciter_strike_timbre = 0.5f;
  patch_.exciter_signature = 0.0f;
  patch_.resonator_geometry = 0.2f;
  patch_.resonator_brightness = 0.5f;
  patch_.resonator_damping = 0.25f;
  patch_.resonator_position = 0.3f;
  patch_.resonator_modulation_frequency = 0.5f / sample_rate_.hz();
  patch_.resonator_modulation_offset = 0.1f;
  patch_.reverb_diffusion = 0.625f;
  patch_.reverb_lp = 0.7f;
  patch_.space = 0.5f;
  // Not used by this part; the original leaves it uninitialized.
  patch_.modulation_frequency = 0.0f;
  previous_gate_ = false;
  active_voice_ = 0;
  
  fill(&silence_[0], &silence_[kMaxBlockSize], 0.0f);
  fill(&note_[0], &note_[kNumVoices], 69.0f);
  
  for (size_t i = 0; i < kNumVoices; ++i) {
    voice_[i].Init(&sample_rate_, &rng_);
  }
  
  if (!reverb_.Init(&sample_rate_, reverb_buffer, reverb_buffer_size)) {
    return false;
  }
  
  scaled_exciter_level_ = 0.0f;
  scaled_resonator_level_ = 0.0f;
  resonator_level_ = 0.0f;
  
  bypass_ = false;
  // Not set by the original, which relies on zeroed memory.
  panic_ = false;
  return true;
}

void Part::Seed(uint32_t* seed, size_t size) {
  // Scramble all bits from the serial number.
  uint32_t signature = 0xf0cacc1a;
  for (size_t i = 0; i < size; ++i) {
    signature ^= seed[i];
    signature = signature * 1664525L + 1013904223L;
  }
  float x;

  x = static_cast<float>(signature & 7) / 8.0f;
  signature >>= 3;
  patch_.resonator_modulation_frequency = (0.4f + 0.8f * x) /
      sample_rate_.hz();
  
  x = static_cast<float>(signature & 7) / 8.0f;
  signature >>= 3;
  patch_.resonator_modulation_offset = 0.05f + 0.1f * x;

  x = static_cast<float>(signature & 7) / 8.0f;
  signature >>= 3;
  patch_.reverb_diffusion = 0.55f + 0.15f * x;

  x = static_cast<float>(signature & 7) / 8.0f;
  signature >>= 3;
  patch_.reverb_lp = 0.7f + 0.2f * x;

  x = static_cast<float>(signature & 7) / 8.0f;
  signature >>= 3;
  patch_.exciter_signature = x;
}

void Part::Process(
    const PerformanceState& performance_state,
    const float* blow_in,
    const float* strike_in,
    float* main,
    float* aux,
    size_t size) {

  // Copy inputs to outputs when bypass mode is enabled.
  if (bypass_ || panic_) {
    if (panic_) {
      // If the resonator is blowing up (this has been observed once before
      // corrective action was taken), reset the state of the filters to 0
      // to prevent the module to freeze with resonators' state blocked at NaN.
      for (size_t i = 0; i < kNumVoices; ++i) {
        voice_[i].Panic();
      }
      resonator_level_ = 0.0f;
      panic_ = false;
    }
    copy(&blow_in[0], &blow_in[size], &aux[0]);
    copy(&strike_in[0], &strike_in[size], &main[0]);
    return;
  }

  // When a new note is played, cycle to the next voice.
  if (performance_state.gate && !previous_gate_) {
    ++active_voice_;
    if (active_voice_ >= kNumVoices) {
      active_voice_ = 0;
    }
  }
  
  previous_gate_ = performance_state.gate;
  note_[active_voice_] = performance_state.note;
  fill(&main[0], &main[size], 0.0f);
  fill(&aux[0], &aux[size], 0.0f);
  
  // Compute the raw signal gain, stereo spread, and reverb parameters from
  // the "space" metaparameter.
  float space = patch_.space >= 1.0f ? 1.0f : patch_.space;
  float raw_gain = space <= 0.05f ? 1.0f : 
    (space <= 0.1f ? 2.0f - space * 20.0f : 0.0f);
  space = space >= 0.1f ? space - 0.1f : 0.0f;
  float spread = space <= 0.7f ? space : 0.7f;
  float reverb_amount = space >= 0.5f ? 1.0f * (space - 0.5f) : 0.0f;
  float reverb_time = 0.35f + 1.2f * reverb_amount;
  
  // Render each voice.
  for (size_t i = 0; i < kNumVoices; ++i) {
    float midi_pitch = note_[i] + performance_state.modulation;
    // Convert the MIDI pitch to a frequency. As in the original it is the
    // frequency over 32 kHz, at any sample rate: the voice's parts convert.
    int32_t pitch = static_cast<int32_t>((midi_pitch + 48.0f) * 256.0f);
    if (pitch < 0) {
      pitch = 0;
    } else if (pitch >= 65535) {
      pitch = 65535;
    }
    // Render the voice signal.
    voice_[i].Process(
        patch_,
        lut_midi_to_f_high[pitch >> 8] * lut_midi_to_f_low[pitch & 0xff],
        performance_state.strength,
        i == active_voice_ && performance_state.gate,
        (i == active_voice_) ? blow_in : silence_,
        (i == active_voice_) ? strike_in : silence_,
        raw_buffer_,
        center_buffer_,
        sides_buffer_,
        size);
    
    // Mixdown.
    for (size_t j = 0; j < size; ++j) {
      float side = sides_buffer_[j] * spread;
      float r = center_buffer_[j] - side;
      float l = center_buffer_[j] + side;;
      main[j] += r;
      aux[j] += l + (raw_buffer_[j] - l) * raw_gain;
    }
  }
  
  // Pre-clipping
  for (size_t i = 0; i < size; ++i) {
    main[i] = SoftLimit(main[i]);
    aux[i] = SoftLimit(aux[i]);
  }
  
  // Metering.
  float exciter_level = voice_[active_voice_].exciter_level();
  float resonator_level = resonator_level_;
  for (size_t i = 0; i < size; ++i) {
    float error = main[i] * main[i] - resonator_level;
    resonator_level += error * (error > 0.0f ? meter_attack_ : meter_decay_);
  }
  resonator_level_ = resonator_level;
  if (resonator_level >= 200.0f) {
    panic_ = true;
  }
  
  exciter_level *= 16.0f;
  scaled_exciter_level_ = exciter_level > 0.1f ? 1.0f : exciter_level;

  resonator_level *= 16.0f;
  scaled_resonator_level_ = resonator_level < 1.0f ? resonator_level : 1.0f;
  
  // Apply reverb.
  reverb_.set_amount(reverb_amount);
  reverb_.set_diffusion(patch_.reverb_diffusion);
  bool freeze = patch_.space >= 1.75f;
  if (freeze) {
    reverb_.set_time(1.0f);
    reverb_.set_input_gain(0.0f);
    reverb_.set_lp(1.0f);
  } else {
    reverb_.set_time(reverb_time);
    reverb_.set_input_gain(0.2f);
    reverb_.set_lp(patch_.reverb_lp);
  }
  reverb_.Process(main, aux, size);
}

}  // namespace elements
