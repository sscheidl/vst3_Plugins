// Copyright 2026 TAUREON.
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
// -----------------------------------------------------------------------------
//
// The sample rate the DSP runs at. The original code is written for 32 kHz:
// in constants, in the contents of its tables and in its buffer sizes.
// Everything that depends on the rate asks this class instead, in terms of
// the original numbers ("a delay of 100 samples at 32 kHz").
//
// At 32 kHz every conversion returns its argument unchanged and the tables
// are the original ones, so the arithmetic is the original arithmetic bit for
// bit. A test holds the port to that.

#ifndef ELEMENTS_DSP_SAMPLE_RATE_H_
#define ELEMENTS_DSP_SAMPLE_RATE_H_

#include "stmlib/stmlib.h"

#include "elements/dsp/dsp.h"

namespace elements {

const size_t kSvfTableSize = 257;

class SampleRate {
 public:
  SampleRate() : hz_(0.0f) { }
  ~SampleRate() { }

  // 32000 (the reference), 48000 or 96000 Hz. False for any other rate; the
  // object then stays as it was. Computes tables, which takes a few
  // milliseconds; a second call with the same rate does nothing.
  static bool Supported(float hz);
  bool Init(float hz);

  inline float hz() const { return hz_; }
  // This rate over 32 kHz, and the other way round.
  inline float ratio() const { return ratio_; }
  inline float inv_ratio() const { return inv_ratio_; }
  inline bool reference() const { return reference_; }
  // Samples per Process() call: 16 at 32 kHz, 24 at 48 kHz, 48 at 96 kHz.
  // Always 0.5 ms, the 2 kHz control rate the envelope table, the mode
  // update schedule and every per-block smoothing are made for.
  inline size_t block_size() const { return block_size_; }

  // A length in samples at 32 kHz, as the nearest whole number of samples
  // that lasts as long at this rate.
  inline size_t Samples(size_t samples) const {
    return reference_
        ? samples
        : static_cast<size_t>(static_cast<float>(samples) * ratio_ + 0.5f);
  }
  // The same for a position that may fall between samples.
  inline float Samples(float samples) const {
    return samples * ratio_;
  }

  // A factor applied once per sample at 32 kHz, e.g. the 0.997 of a decay:
  // the factor that does the same per second at this rate.
  float Decay(float factor) const;
  // A one-pole coefficient (y += c * (x - y)) with the same time constant.
  inline float OnePole(float coefficient) const {
    return reference_ ? coefficient : 1.0f - Decay(1.0f - coefficient);
  }
  // A probability per sample at 32 kHz with the same rate of events per
  // second.
  inline float Probability(float probability) const {
    return probability * inv_ratio_;
  }
  // White noise with one random value per sample spreads its power up to
  // half the sample rate. This gain keeps its density per Hz, and with it
  // what a filter or a resonator makes of it, as at 32 kHz.
  inline float noise_gain() const { return noise_gain_; }

  // The tables of the exciters' low-pass, indexed by cutoff as the original
  // ones. Same cutoff frequencies in Hz; a pulse of svf_gain() samples
  // height carries the same area in seconds.
  inline const float* svf_gain() const { return svf_gain_; }
  inline const float* svf_g() const { return svf_g_; }
  inline const float* svf_h() const { return svf_h_; }

  // The coefficients of MatchedLowPass (see matched_low_pass.h), indexed by
  // cutoff like the tables above: gain, three numerator coefficients after
  // the leading 1, two denominator coefficients. Not used at 32 kHz.
  inline const float* matched_b0() const { return matched_b0_; }
  inline const float* matched_n1() const { return matched_n1_; }
  inline const float* matched_n2() const { return matched_n2_; }
  inline const float* matched_n3() const { return matched_n3_; }
  inline const float* matched_a1() const { return matched_a1_; }
  inline const float* matched_a2() const { return matched_a2_; }

 private:
  void ComputeMatchedLowPass();

  float hz_;
  float ratio_;
  float inv_ratio_;
  float noise_gain_;
  bool reference_;
  size_t block_size_;

  const float* svf_gain_;
  const float* svf_g_;
  const float* svf_h_;
  float svf_gain_table_[kSvfTableSize];
  float svf_g_table_[kSvfTableSize];
  float svf_h_table_[kSvfTableSize];

  float matched_b0_[kSvfTableSize];
  float matched_n1_[kSvfTableSize];
  float matched_n2_[kSvfTableSize];
  float matched_n3_[kSvfTableSize];
  float matched_a1_[kSvfTableSize];
  float matched_a2_[kSvfTableSize];

  DISALLOW_COPY_AND_ASSIGN(SampleRate);
};

}  // namespace elements

#endif  // ELEMENTS_DSP_SAMPLE_RATE_H_
