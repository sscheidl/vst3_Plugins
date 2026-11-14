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
// Utility DSP routines.
//
// Modified 2026 for vst3_Plugins (TAUREON): the sample rate is no longer the
// constant kSampleRate but set at run time, see sample_rate.h and UPSTREAM.md.

#ifndef ELEMENTS_DSP_DSP_H_
#define ELEMENTS_DSP_DSP_H_

#include "stmlib/stmlib.h"

#include <cmath>

namespace elements {

// The rate of the hardware. The tables in resources.cc and every constant
// taken over from the original are meant for it.
static const float kReferenceSampleRate = 32000.0f;
// The hardware renders 16 samples per call: a control rate of 2 kHz. The
// block grows with the sample rate so that the control rate stays.
const size_t kReferenceBlockSize = 16;

// The highest supported rate is this many times the reference: 96 kHz.
// Buffers are sized for it.
const size_t kMaxSampleRateRatio = 3;
const size_t kMaxBlockSize = kReferenceBlockSize * kMaxSampleRateRatio;

}  // namespace elements

#endif  // ELEMENTS_DSP_DSP_H_
