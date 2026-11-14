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
// The low-pass of the pulse and bow exciters at a sample rate other than
// 32 kHz.
//
// The original filters with a state variable filter whose damping r is 2:
// two equal one-pole sections, each
//
//   y[n] = G (x[n] + x[n - 1]) + p y[n - 1],  G = g / (1 + g), p = (1 - g) / (1 + g)
//
// with g from the table lut_approx_svf_g. Such a section has a zero at half
// the sample rate, 16 kHz: towards it the response falls away much faster
// than the nominal 12 dB per octave, and the highest partials of the
// resonator get correspondingly little. A filter with the same cutoff at
// 48 kHz would not do that; bright sounds would come out brighter.
//
// So each section gets a counterpart with the same response up to 16 kHz:
// the zero stays at 16 kHz, as a pair of zeros; the pole and one further zero
// are placed so that the response in dB is closest to the original's from
// 50 Hz to 14.5 kHz. A pole with p < 0, which the original has for cutoffs
// above 8 kHz, rings at 16 kHz and becomes a pair of poles there. SampleRate
// computes the coefficients once, for every entry of the cutoff table.

#ifndef ELEMENTS_DSP_MATCHED_LOW_PASS_H_
#define ELEMENTS_DSP_MATCHED_LOW_PASS_H_

#include "stmlib/stmlib.h"

#include "elements/dsp/sample_rate.h"

namespace elements {

class MatchedLowPass {
 public:
  MatchedLowPass() { }
  ~MatchedLowPass() { }

  void Init(const SampleRate* sample_rate) {
    sample_rate_ = sample_rate;
    b0_ = 0.0f;
    n1_ = 0.0f;
    n2_ = 0.0f;
    n3_ = 0.0f;
    a1_ = 0.0f;
    a2_ = 0.0f;
    for (size_t i = 0; i < 2; ++i) {
      x1_[i] = x2_[i] = x3_[i] = y1_[i] = y2_[i] = 0.0f;
    }
  }

  // `cutoff_index` as for lut_approx_svf_g, 0 to 256.
  inline void set_cutoff(uint32_t cutoff_index) {
    b0_ = sample_rate_->matched_b0()[cutoff_index];
    n1_ = sample_rate_->matched_n1()[cutoff_index];
    n2_ = sample_rate_->matched_n2()[cutoff_index];
    n3_ = sample_rate_->matched_n3()[cutoff_index];
    a1_ = sample_rate_->matched_a1()[cutoff_index];
    a2_ = sample_rate_->matched_a2()[cutoff_index];
  }

  inline void Process(const float* in, float* out, size_t size) {
    const float n1 = n1_;
    const float n2 = n2_;
    const float n3 = n3_;
    while (size--) {
      float x = *in++;
      for (size_t i = 0; i < 2; ++i) {
        float y = b0_ * (x + n1 * x1_[i] + n2 * x2_[i] + n3 * x3_[i]) -
            a1_ * y1_[i] - a2_ * y2_[i];
        x3_[i] = x2_[i];
        x2_[i] = x1_[i];
        x1_[i] = x;
        y2_[i] = y1_[i];
        y1_[i] = y;
        x = y;
      }
      *out++ = x;
    }
  }

 private:
  const SampleRate* sample_rate_;
  float b0_;
  float n1_;
  float n2_;
  float n3_;
  float a1_;
  float a2_;
  float x1_[2];
  float x2_[2];
  float x3_[2];
  float y1_[2];
  float y2_[2];

  DISALLOW_COPY_AND_ASSIGN(MatchedLowPass);
};

}  // namespace elements

#endif  // ELEMENTS_DSP_MATCHED_LOW_PASS_H_
