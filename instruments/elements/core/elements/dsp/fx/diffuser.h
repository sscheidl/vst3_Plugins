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
// Granular diffuser.
//
// Modified 2026 for vst3_Plugins (TAUREON): the delay lengths follow the
// sample rate; the numbers in the code are the original ones, for 32 kHz.
// See UPSTREAM.md.

#ifndef ELEMENTS_DSP_FX_DIFFUSER_H_
#define ELEMENTS_DSP_FX_DIFFUSER_H_

#include "stmlib/stmlib.h"

#include "elements/dsp/dsp.h"
#include "elements/dsp/fx/fx_engine.h"
#include "elements/dsp/sample_rate.h"

namespace elements {

const size_t kNumDiffuserDelays = 4;

// Lengths of the all-passes at 32 kHz.
const int32_t kDiffuserDelayLengths[kNumDiffuserDelays] = {
  126, 180, 269, 444
};

// The original buffer holds 1024 samples, for 32 kHz. Room for 96 kHz.
const size_t kDiffuserBufferSize = 4096;

class Diffuser {
 public:
  Diffuser() { }
  ~Diffuser() { }

  // `buffer` holds kDiffuserBufferSize samples.
  void Init(const SampleRate* sample_rate, float* buffer) {
    int32_t lengths[kNumDiffuserDelays];
    for (size_t i = 0; i < kNumDiffuserDelays; ++i) {
      lengths[i] = static_cast<int32_t>(
          sample_rate->Samples(static_cast<size_t>(kDiffuserDelayLengths[i])));
    }
    // The smallest power of two that holds the delay lines: 1024 at 32 kHz.
    size_t size = 1024;
    while (size < E::Required(lengths, kNumDiffuserDelays) &&
           size < kDiffuserBufferSize) {
      size <<= 1;
    }
    engine_.Init(buffer, size);
    engine_.Allocate(delay_, lengths, kNumDiffuserDelays);
  }

  void Process(float* in_out, size_t size) {
    const E::DelayLine& ap1 = delay_[0];
    const E::DelayLine& ap2 = delay_[1];
    const E::DelayLine& ap3 = delay_[2];
    const E::DelayLine& ap4 = delay_[3];
    E::Context c;
    const float kap = 0.625f;
    while (size--) {
      engine_.Start(&c);
      c.Read(*in_out);
      c.Read(ap1 TAIL, kap);
      c.WriteAllPass(ap1, -kap);
      c.Read(ap2 TAIL, kap);
      c.WriteAllPass(ap2, -kap);
      c.Read(ap3 TAIL, kap);
      c.WriteAllPass(ap3, -kap);
      c.Read(ap4 TAIL, kap);
      c.WriteAllPass(ap4, -kap);
      c.Write(*in_out, 0.0f);
      ++in_out;
    }
  }

 private:
  typedef FxEngine<FORMAT_32_BIT> E;
  E engine_;
  E::DelayLine delay_[kNumDiffuserDelays];

  DISALLOW_COPY_AND_ASSIGN(Diffuser);
};

}  // namespace elements

#endif  // ELEMENTS_DSP_FX_DIFFUSER_H_
