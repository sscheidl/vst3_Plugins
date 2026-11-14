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
// Resonator.
//
// Modified 2026 for vst3_Plugins (TAUREON): at any sample rate every mode has the
// frequency, the decay and the level it has at 32 kHz.
// See UPSTREAM.md.

#include "elements/dsp/resonator.h"

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/cosine_oscillator.h"

#include "elements/dsp/dsp.h"
#include "elements/resources.h"

namespace elements {

using namespace std;
using namespace stmlib;

void Resonator::Init(const SampleRate* sample_rate) {
  sample_rate_ = sample_rate;

  for (size_t i = 0; i < kMaxModes; ++i) {
    f_[i].Init();
    mode_gain_[i] = 1.0f;
  }
  if (!sample_rate->reference()) {
    // Svf::Init() leaves every mode at 0.01 of the sample rate with a
    // resonance of 100, and half of the modes above the 25th keep that until
    // the second block. The same start filter as at 32 kHz, in time.
    const Mode start = ConvertMode(
        OnePole::tan<FREQUENCY_DIRTY>(0.01f), 100.0f, sample_rate->ratio());
    for (size_t i = 0; i < kMaxModes; ++i) {
      f_[i].set_g_q(start.g, start.resonance);
      mode_gain_[i] = start.gain;
    }
  }

  for (size_t i = 0; i < kMaxBowedModes; ++i) {
    f_bow_[i].Init();
    d_bow_[i].Init();
  }
  
  set_frequency(220.0f / kReferenceSampleRate);
  set_geometry(0.25f);
  set_brightness(0.5f);
  set_damping(0.3f);
  set_position(0.999f);
  previous_position_ = 0.0f;
  set_resolution(kMaxModes);
  
  bow_signal_ = 0.0f;
  // Not set by the original, which relies on zeroed memory.
  clock_divider_ = 0;
  lfo_phase_ = 0.0f;
  set_modulation_frequency(0.0f);
  set_modulation_offset(0.0f);
}

namespace {

// The ringing of an Svf band-pass: its poles are rho * e^(+-j phi), and after
// the first sample its answer to a pulse of 1 is
// amplitude * rho^n * sin(n phi + phase). For the filter with g and
// r = 1 / Q the transfer function is
//   g (1 - z^-2) / ((1 + r g + g^2) - 2 (1 - g^2) z^-1 + (1 - r g + g^2) z^-2),
// and the residue of its pole p gives
//   amplitude = g |p^2 - 1| / ((1 + r g + g^2) rho^2 sin(phi)),
//   |p^2 - 1| = sqrt((1 - rho^2)^2 + 4 rho^2 sin(phi)^2).
inline float RingingAmplitude(
    float g, float r, float rho2, float one_minus_rho2, float sin_phi) {
  const float distance = sqrtf(
      one_minus_rho2 * one_minus_rho2 + 4.0f * rho2 * sin_phi * sin_phi);
  return g * distance / ((1.0f + r * g + g * g) * rho2 * sin_phi);
}

}  // namespace

// The filter is the bilinear image of s / (s^2 + s / Q + 1) at g, with poles
// p = (1 + g u) / (1 - g u), u = -1 / (2 Q) + j sqrt(1 - 1 / (4 Q^2)), |u| = 1.
// With x = g r / (1 + g^2) that gives
//   rho^2 = (1 - x) / (1 + x),  ln(rho^2) = -2 atanh(x),
//   phi = atan2(2 g Im(u), 1 - g^2).
// The same ringing at `ratio` times the rate has the pole p^(1 / ratio):
// ln(rho^2) and phi divided by the ratio. The filter with that pole follows
// from g u = (p - 1) / (p + 1), with |u| = 1:
//   g = sqrt((1 - rho^2)^2 + 4 rho^2 sin(phi)^2) / (1 + 2 rho cos(phi) + rho^2),
//   Q = sqrt((1 - rho^2)^2 + 4 rho^2 sin(phi)^2) / (2 (1 - rho^2)).
// 1 - rho^2 is taken with expm1, so that high resonances keep their
// precision. For a high Q this is the conversion of the first port, which
// kept the decay per sample sin(angle) / (2 Q) per second.
Resonator::Mode Resonator::ConvertMode(float g32, float q32, float ratio) {
  const float inv_ratio = 1.0f / ratio;
  const float r32 = 1.0f / q32;
  const float x = g32 * r32 / (1.0f + g32 * g32);
  const float one_minus_rho2_32 = 2.0f * x / (1.0f + x);
  const float rho2_32 = 1.0f - one_minus_rho2_32;
  const float half_r = 0.5f * r32;
  const float u_imaginary = sqrtf(max(0.0f, 1.0f - half_r * half_r));
  const float phi_32 = atan2f(2.0f * g32 * u_imaginary, 1.0f - g32 * g32);

  const float one_minus_rho2 = -expm1f(-2.0f * atanhf(x) * inv_ratio);
  const float rho2 = 1.0f - one_minus_rho2;
  const float rho = sqrtf(rho2);
  const float phi = phi_32 * inv_ratio;
  const float sin_phi = sinf(phi);
  const float distance = sqrtf(
      one_minus_rho2 * one_minus_rho2 + 4.0f * rho2 * sin_phi * sin_phi);

  Mode mode;
  mode.g = distance / (1.0f + 2.0f * rho * cosf(phi) + rho2);
  mode.resonance = distance / (2.0f * one_minus_rho2);
  // A pulse of the same area is 1 / ratio as high.
  mode.gain = RingingAmplitude(g32, r32, rho2_32, one_minus_rho2_32,
                               sinf(phi_32)) /
      (ratio * RingingAmplitude(mode.g, 1.0f / mode.resonance, rho2,
                                one_minus_rho2, sin_phi));
  return mode;
}

size_t Resonator::ComputeFilters() {
  ++clock_divider_;
  float stiffness = Interpolate(lut_stiffness, geometry_, 256.0f);
  float harmonic = frequency_;
  float stretch_factor = 1.0f; 
  float q = 500.0f * Interpolate(
      lut_4_decades,
      damping_ * 0.8f,
      256.0f);
  const bool reference = sample_rate_->reference();
  const float ratio = sample_rate_->ratio();
  const float inv_ratio = sample_rate_->inv_ratio();
  float brightness_attenuation = 1.0f - geometry_;
  // Reduces the range of brightness when geometry is very low, to prevent
  // clipping.
  brightness_attenuation *= brightness_attenuation;
  brightness_attenuation *= brightness_attenuation;
  brightness_attenuation *= brightness_attenuation;
  float brightness = brightness_ * (1.0f - 0.2f * brightness_attenuation);
  float q_loss = brightness * (2.0f - brightness) * 0.85f + 0.15f;
  float q_loss_damping_rate = geometry_ * (2.0f - geometry_) * 0.1f;
  size_t num_modes = 0;
  for (size_t i = 0; i < min(kMaxModes, resolution_); ++i) {
    // Update the first 24 modes every time (2kHz). The higher modes are
    // refreshed as a slowest rate.
    bool update = i <= 24 || ((i & 1) == (clock_divider_ & 1));
    float partial_frequency = harmonic * stretch_factor;
    if (partial_frequency >= 0.49f) {
      partial_frequency = 0.49f;
    } else {
      num_modes = i + 1;
    }
    if (update) {
      // The mode as the original makes it at 32 kHz: g is the tangent of
      // half its angle per sample, from an approximation that leaves the
      // highest modes flat; the resonance grows with the frequency.
      float g = stmlib::OnePole::tan<FREQUENCY_FAST>(partial_frequency);
      float resonance = 1.0f + partial_frequency * q;
      float g_bow = g;
      float bow_resonance = 1.0f + partial_frequency * 1500.0f;
      size_t period = 1.0f / partial_frequency;
      while (period >= kMaxBowedPeriod) period >>= 1;
      if (!reference && g > 0.0f) {
        // The same mode at this rate: frequency, flatness included, decay
        // and level of its ringing as at 32 kHz (see ConvertMode).
        const float g32 = g;
        const Mode mode = ConvertMode(g32, resonance, ratio);
        g = mode.g;
        resonance = mode.resonance;
        mode_gain_[i] = mode.gain;
        if (i < kMaxBowedModes) {
          // A bowed mode is a band-pass in a delay loop, normalized to a gain
          // of 1 at its centre. What counts there is that centre, which must
          // stay on the comb of the delay, and the width of the band: the
          // same angle per second, and Q scaled by k, the ratio of the sines
          // of the two angles per second, which keeps the band as wide in
          // hertz. The delay lasts as long as at 32 kHz, to the nearest
          // sample.
          g_bow = tanf(atanf(g32) * inv_ratio);
          const float k = ratio * (g_bow * (1.0f + g32 * g32)) /
              (g32 * (1.0f + g_bow * g_bow));
          bow_resonance *= k;
          period = sample_rate_->Samples(period);
        }
      }
      f_[i].set_g_q(g, resonance);
      if (i < kMaxBowedModes) {
        d_bow_[i].set_delay(period);
        f_bow_[i].set_g_q(g_bow, bow_resonance);
      }
    }
    stretch_factor += stiffness;
    if (stiffness < 0.0f) {
      // Make sure that the partials do not fold back into negative frequencies.
      stiffness *= 0.93f;
    } else {
      // This helps adding a few extra partials in the highest frequencies.
      stiffness *= 0.98f;
    }
    // This prevents the highest partials from decaying too fast.
    q_loss += q_loss_damping_rate * (1.0f - q_loss);
    harmonic += frequency_;
    q *= q_loss;
  }
  
  return num_modes;
}

void Resonator::Process(
    const float* bow_strength,
    const float* in,
    float* center,
    float* sides,
    size_t size) {
  size_t num_modes = ComputeFilters();
  size_t num_banded_wg = min(kMaxBowedModes, num_modes);
  // Linearly interpolate position. This parameter is extremely sensitive to
  // zipper noise.
  float position_increment = (position_ - previous_position_) / size;
  while (size--) {
    float s;

    // 0.5 Hz LFO used to modulate the position of the stereo side channel.
    lfo_phase_ += modulation_frequency_;
    if (lfo_phase_ >= 1.0f) {
      lfo_phase_ -= 1.0f;
    }
    previous_position_ += position_increment;
    float lfo = lfo_phase_ > 0.5f ? 1.0f - lfo_phase_ : lfo_phase_;
    CosineOscillator amplitudes;
    CosineOscillator aux_amplitudes;
    amplitudes.Init<COSINE_OSCILLATOR_APPROXIMATE>(previous_position_);
    aux_amplitudes.Init<COSINE_OSCILLATOR_APPROXIMATE>(
        modulation_offset_ + lfo);
  
    // Render normal modes.
    float input = *in++ * 0.125f;
    float sum_center = 0.0f;
    float sum_side = 0.0f;

    // Note: For a steady sound, the correct way of simulating the effect of
    // a pickup is to use a comb filter. But it sounds very flange-y when
    // modulated, even mildly, and incur a slight delay/smearing of the
    // attacks.
    // Thus, we directly apply the comb filter in the frequency domain by
    // adjusting the amplitude of each mode in the sum. Because the
    // partials may not be in an integer ratios, what we are doing here is
    // approximative when the stretch factor is non null.
    // It sounds interesting nevertheless.
    amplitudes.Start();
    aux_amplitudes.Start();
    for (size_t i = 0; i < num_modes; i++) {
      s = f_[i].Process<FILTER_MODE_BAND_PASS>(input) * mode_gain_[i];
      sum_center += s * amplitudes.Next();
      sum_side += s * aux_amplitudes.Next();
    }
    *sides++ = sum_side - sum_center;
    
    // Render bowed modes.
    float bow_signal = 0.0f;
    input += bow_signal_;
    amplitudes.Start();
    for (size_t i = 0; i < num_banded_wg; ++i) {
      s = 0.99f * d_bow_[i].Read();
      bow_signal += s;
      s = f_bow_[i].Process<FILTER_MODE_BAND_PASS_NORMALIZED>(input + s);
      d_bow_[i].Write(s);
      sum_center += s * amplitudes.Next() * 8.0f;
    }
    bow_signal_ = BowTable(bow_signal, *bow_strength++);
    *center++ = sum_center;
  }
}

}  // namespace elements
