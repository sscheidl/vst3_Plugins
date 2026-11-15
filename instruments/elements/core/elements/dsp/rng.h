// Copyright 2012 Emilie Gillet.
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
// Pseudo random number generator.
//
// Derived 2026 for vst3_Plugins (TAUREON) from stmlib/utils/random.h: the
// same generator with the same start value, but its state belongs to an
// object instead of the whole program. Several instruments in one process
// then neither share a sequence nor write to one variable from two threads.

#ifndef ELEMENTS_DSP_RNG_H_
#define ELEMENTS_DSP_RNG_H_

#include "stmlib/stmlib.h"

namespace elements {

class Rng {
 public:
  Rng() { }
  ~Rng() { }

  // The start value of stmlib::Random.
  static const uint32_t kDefaultSeed = 0x21;

  inline void Seed(uint32_t seed) {
    state_ = seed;
  }

  inline uint32_t state() const { return state_; }

  inline uint32_t GetWord() {
    state_ = state_ * 1664525L + 1013904223L;
    return state_;
  }

 private:
  uint32_t state_;

  DISALLOW_COPY_AND_ASSIGN(Rng);
};

}  // namespace elements

#endif  // ELEMENTS_DSP_RNG_H_
