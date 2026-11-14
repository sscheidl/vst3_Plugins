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
// Modified 2026 for vst3_Plugins (TAUREON): the delay line, the reflection filter and
// the output low-pass keep their length and their response in time at any
// sample rate.
// See UPSTREAM.md.

#include "elements/dsp/tube.h"

#include <algorithm>

#include "stmlib/dsp/dsp.h"

namespace elements {

void Tube::Init(const SampleRate* sample_rate) {
  sample_rate_ = sample_rate;
  delay_size_ = static_cast<int32_t>(sample_rate->Samples(kTubeDelaySize));
  // One sample at 32 kHz is ratio() samples here: 1, 1.5 or 3.
  float zero_delay = sample_rate->Samples(1.0f) - 1.0f;
  zero_delay_integral_ = static_cast<int32_t>(zero_delay);
  zero_delay_fractional_ = zero_delay -
      static_cast<float>(zero_delay_integral_);

  std::fill(&zero_state_[0], &zero_state_[kTubeZeroHistorySize], 0.0f);
  pole_state_ = 0.0f;
  delay_ptr_ = 0;
  // Not cleared by the original, which relies on zeroed memory.
  std::fill(&delay_line_[0], &delay_line_[kTubeBufferSize], 0.0f);
}

void Tube::Process(
    float frequency,
    float envelope,
    float damping,
    float timbre,
    float* input_output,
    float gain,
    size_t size) {
  // `frequency` is the frequency over 32 kHz, as in the original.
  float delay = sample_rate_->ratio() / frequency;
  while (delay >= float(delay_size_)) {
    delay *= 0.5f;
  }
  MAKE_INTEGRAL_FRACTIONAL(delay);
  
  if (envelope >= 1.0f) envelope = 1.0f;
  
  damping = 3.6f - damping * 1.8f;
  // The coefficient as the original has it at 32 kHz, then the one with
  // the same time constant at this rate.
  float lpf_coefficient = frequency * (1.0f + timbre * timbre * 256.0f);
  if (lpf_coefficient >= 0.995f) lpf_coefficient = 0.995f;
  lpf_coefficient = sample_rate_->OnePole(lpf_coefficient);
  
  const int32_t delay_size = delay_size_;
  const int32_t zi = zero_delay_integral_;
  const float zf = zero_delay_fractional_;
  
  int32_t d = delay_ptr_;;
  while (size--) {
    float breath = *input_output * damping + 0.8f;
    float a = delay_line_[(d + delay_integral) % delay_size];
    float b = delay_line_[(d + delay_integral + 1) % delay_size];
    float in = a + (b - a) * delay_fractional;
    float zero = zero_state_[zi];
    if (zf != 0.0f) {
      zero += (zero_state_[zi + 1] - zero) * zf;
    }
    float pressure_delta = -0.95f * (in * envelope + zero) - breath;
    for (size_t i = kTubeZeroHistorySize - 1; i > 0; --i) {
      zero_state_[i] = zero_state_[i - 1];
    }
    zero_state_[0] = in;
    
    float reed = pressure_delta * -0.2f + 0.8f;
    float out = pressure_delta * reed + breath;
    
    CONSTRAIN(out, -5.0f, 5.0f);
    delay_line_[d] = out * 0.5f;
    
    --d;
    if (d < 0) {
      d = delay_size - 1;
    }
    pole_state_ += lpf_coefficient * (out - pole_state_);
    *input_output++ += gain * envelope * pole_state_;
  }
  delay_ptr_ = d;
}

}  // namespace elements
