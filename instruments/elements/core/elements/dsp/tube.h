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
// Simple waveguide tube.
//
// Modified 2026 for vst3_Plugins (TAUREON): runs at the sample rate given to Init().
// See UPSTREAM.md.

#ifndef ELEMENTS_DSP_TUBE_H_
#define ELEMENTS_DSP_TUBE_H_

#include "stmlib/stmlib.h"

#include <cmath>
#include <algorithm>

#include "elements/dsp/dsp.h"
#include "elements/dsp/sample_rate.h"

namespace elements {

// The length of the delay line at 32 kHz; it is as long in time at other
// rates. The buffer has room for 96 kHz.
const size_t kTubeDelaySize = 2048;
const size_t kTubeBufferSize = kTubeDelaySize * kMaxSampleRateRatio;
// The reflection adds the wave to itself one sample (at 32 kHz) earlier.
const size_t kTubeZeroHistorySize = kMaxSampleRateRatio + 1;

class Tube {
 public:
  Tube() { }
  ~Tube() { }
  
  void Init(const SampleRate* sample_rate);
  void Process(
      float frequency,
      float envelope,
      float damping,
      float timbre,
      float* input_output,
      float gain,
      size_t size);

 private:
  const SampleRate* sample_rate_;
  int32_t delay_size_;
  // The delay of the reflection in samples, less one: whole part, fraction.
  int32_t zero_delay_integral_;
  float zero_delay_fractional_;

  int32_t delay_ptr_;
  // The last values of the wave, newest first; the original's zero_state_
  // is the first of them.
  float zero_state_[kTubeZeroHistorySize];
  float pole_state_;
  float delay_line_[kTubeBufferSize];

  DISALLOW_COPY_AND_ASSIGN(Tube);
};

}  // namespace elements

#endif  // ELEMENTS_DSP_TUBE_H_
