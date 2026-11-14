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
// Modified 2026 for vst3_Plugins (TAUREON): runs at the sample rate given to Init(),
// in blocks of block_size() samples; owns the random generator of its
// exciters; without the string models and the "ominous voice" of the later
// firmware versions.
// See UPSTREAM.md.

#ifndef ELEMENTS_DSP_PART_H_
#define ELEMENTS_DSP_PART_H_

#include "stmlib/stmlib.h"

#include "elements/dsp/fx/reverb.h"
#include "elements/dsp/patch.h"
#include "elements/dsp/rng.h"
#include "elements/dsp/sample_rate.h"
#include "elements/dsp/voice.h"

namespace elements {

struct PerformanceState {
  bool gate;
  float note;
  float modulation;
  float strength;
};

// Polyphony is actually possible, but you have to reduce the number of modes
// to 16, and this doesn't sound very good...
const size_t kNumVoices = 1;

class Part {
 public:
  Part() { }
  ~Part() { }
  
  // The number of elements the reverb buffer needs at a sample rate, 0 for
  // a rate that is not supported: 32768 at 32 and 48 kHz, 65536 at 96 kHz.
  static size_t ReverbBufferSize(float sample_rate);
  
  // `sample_rate` is 32000 (the hardware's rate), 48000 or 96000;
  // `reverb_buffer` holds `reverb_buffer_size` elements, at least
  // ReverbBufferSize(sample_rate) and a power of two. False, with the part
  // unusable, if either does not fit.
  bool Init(
      float sample_rate,
      uint16_t* reverb_buffer,
      size_t reverb_buffer_size);
  
  // Samples per call of Process(): 0.5 ms, i.e. 16 at 32 kHz, 24 at 48 kHz,
  // 48 at 96 kHz. The envelopes and every per-block smoothing count calls.
  inline size_t block_size() const { return sample_rate_.block_size(); }
  inline const SampleRate& sample_rate() const { return sample_rate_; }
  
  // `size` is block_size().
  void Process(
      const PerformanceState& performance_state,
      const float* blow_in,
      const float* strike_in,
      float* main,
      float* aux,
      size_t n);

  inline Patch* mutable_patch() { return &patch_; }
  // The random generator of the exciters; Init() seeds it with
  // Rng::kDefaultSeed.
  inline Rng* mutable_rng() { return &rng_; }
  
  void Seed(uint32_t* seed, size_t size);
  // Resets the resonator at the next Process() call, which then outputs its
  // inputs.
  inline void Panic() { panic_ = true; }
  
  // For metering.
  inline float exciter_level() const { return scaled_exciter_level_; }
  inline float resonator_level() const { return scaled_resonator_level_; }
  inline bool gate() const { return previous_gate_; }
  inline bool bypass() const { return bypass_; }
  inline void set_bypass(bool bypass) { bypass_ = bypass; }
  
 private:
  SampleRate sample_rate_;
  Rng rng_;
  // The followers of the level meter, as they are at this sample rate.
  float meter_attack_;
  float meter_decay_;

  Patch patch_;
  Voice voice_[kNumVoices];
  
  bool panic_;
  bool bypass_;
  bool previous_gate_;
  float note_[kNumVoices];
  
  size_t num_voices_;
  size_t active_voice_;
  
  float silence_[kMaxBlockSize];
  
  float raw_buffer_[kMaxBlockSize];
  float center_buffer_[kMaxBlockSize];
  float sides_buffer_[kMaxBlockSize];
  
  float scaled_exciter_level_;
  float scaled_resonator_level_;
  float resonator_level_;
  
  Reverb reverb_;
  
  DISALLOW_COPY_AND_ASSIGN(Part);
};

}  // namespace elements

#endif  // ELEMENTS_DSP_PART_H_
