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
// Base class for building reverb.
//
// Modified 2026 for vst3_Plugins (TAUREON): the buffer size and the lengths
// of the delay lines were template arguments, fixed for 32 kHz. They are now
// set at run time (Init, Allocate), so that the lengths can follow the sample
// rate. The arithmetic of Context is the original one. See UPSTREAM.md.

#ifndef ELEMENTS_DSP_FX_FX_ENGINE_H_
#define ELEMENTS_DSP_FX_FX_ENGINE_H_

#include <algorithm>

#include "stmlib/stmlib.h"

#include "stmlib/dsp/dsp.h"
#include "stmlib/dsp/cosine_oscillator.h"

namespace elements {

#define TAIL , -1

enum Format {
  FORMAT_12_BIT,
  FORMAT_16_BIT,
  FORMAT_32_BIT
};

enum LFOIndex {
  LFO_1,
  LFO_2
};

template<Format format>
struct DataType { };

template<>
struct DataType<FORMAT_12_BIT> {
  typedef uint16_t T;

  static inline float Decompress(T value) {
    return static_cast<float>(static_cast<int16_t>(value)) / 4096.0f;
  }

  static inline T Compress(float value) {
    return static_cast<uint16_t>(
        stmlib::Clip16(static_cast<int32_t>(value * 4096.0f)));
  }
};

template<>
struct DataType<FORMAT_16_BIT> {
  typedef uint16_t T;

  static inline float Decompress(T value) {
    return static_cast<float>(static_cast<int16_t>(value)) / 32768.0f;
  }

  static inline T Compress(float value) {
    return static_cast<uint16_t>(
        stmlib::Clip16(static_cast<int32_t>(value * 32768.0f)));
  }
};

template<>
struct DataType<FORMAT_32_BIT> {
  typedef float T;

  static inline float Decompress(T value) {
    return value;;
  }

  static inline T Compress(float value) {
    return value;
  }
};

// A delay line inside the engine's buffer: where it starts and how long it
// is. Filled in by FxEngine::Allocate().
struct FxDelayLine {
  int32_t base;
  int32_t length;
};

template<Format format = FORMAT_12_BIT>
class FxEngine {
 public:
  typedef typename DataType<format>::T T;
  typedef FxDelayLine DelayLine;
  FxEngine() { }
  ~FxEngine() { }

  // `size` is the number of elements in `buffer`, a power of two.
  void Init(T* buffer, size_t size) {
    buffer_ = buffer;
    size_ = static_cast<int32_t>(size);
    mask_ = size_ - 1;
    std::fill(&buffer_[0], &buffer_[size], T(0));
    write_ptr_ = 0;
    // The original leaves the oscillators of an engine without modulation
    // (the diffuser's) uninitialized; Start() reads them all the same.
    SetLFOFrequency(LFO_1, 0.0f);
    SetLFOFrequency(LFO_2, 0.0f);
  }

  // Lays `count` delay lines of the given lengths one after the other, as
  // the original Reserve<> chain did: each takes its length plus one
  // element. False if they do not fit into the buffer.
  bool Allocate(DelayLine* lines, const int32_t* lengths, size_t count) {
    int32_t base = 0;
    for (size_t i = 0; i < count; ++i) {
      lines[i].base = base;
      lines[i].length = lengths[i];
      base += lengths[i] + 1;
    }
    return base - 1 <= size_;
  }

  // The number of buffer elements a set of delay lines needs.
  static size_t Required(const int32_t* lengths, size_t count) {
    size_t total = 0;
    for (size_t i = 0; i < count; ++i) {
      total += static_cast<size_t>(lengths[i]) + 1;
    }
    return total - 1;
  }

  class Context {
   friend class FxEngine;
   public:
    Context() { }
    ~Context() { }

    inline void Load(float value) {
      accumulator_ = value;
    }

    inline void Read(float value, float scale) {
      accumulator_ += value * scale;
    }

    inline void Read(float value) {
      accumulator_ += value;
    }

    inline void Write(float& value) {
      value = accumulator_;
    }

    inline void Write(float& value, float scale) {
      value = accumulator_;
      accumulator_ *= scale;
    }

    inline void Write(const DelayLine& d, int32_t offset, float scale) {
      T w = DataType<format>::Compress(accumulator_);
      if (offset == -1) {
        buffer_[(write_ptr_ + d.base + d.length - 1) & mask_] = w;
      } else {
        buffer_[(write_ptr_ + d.base + offset) & mask_] = w;
      }
      accumulator_ *= scale;
    }

    inline void Write(const DelayLine& d, float scale) {
      Write(d, 0, scale);
    }

    inline void WriteAllPass(const DelayLine& d, int32_t offset, float scale) {
      Write(d, offset, scale);
      accumulator_ += previous_read_;
    }

    inline void WriteAllPass(const DelayLine& d, float scale) {
      WriteAllPass(d, 0, scale);
    }

    inline void Read(const DelayLine& d, int32_t offset, float scale) {
      T r;
      if (offset == -1) {
        r = buffer_[(write_ptr_ + d.base + d.length - 1) & mask_];
      } else {
        r = buffer_[(write_ptr_ + d.base + offset) & mask_];
      }
      float r_f = DataType<format>::Decompress(r);
      previous_read_ = r_f;
      accumulator_ += r_f * scale;
    }

    inline void Read(const DelayLine& d, float scale) {
      Read(d, 0, scale);
    }

    inline void Lp(float& state, float coefficient) {
      state += coefficient * (accumulator_ - state);
      accumulator_ = state;
    }

    inline void Hp(float& state, float coefficient) {
      state += coefficient * (accumulator_ - state);
      accumulator_ -= state;
    }

    inline void Interpolate(const DelayLine& d, float offset, float scale) {
      MAKE_INTEGRAL_FRACTIONAL(offset);
      float a = DataType<format>::Decompress(
          buffer_[(write_ptr_ + offset_integral + d.base) & mask_]);
      float b = DataType<format>::Decompress(
          buffer_[(write_ptr_ + offset_integral + d.base + 1) & mask_]);
      float x = a + (b - a) * offset_fractional;
      previous_read_ = x;
      accumulator_ += x * scale;
    }

    inline void Interpolate(
        const DelayLine& d,
        float offset,
        LFOIndex index,
        float amplitude,
        float scale) {
      offset += amplitude * lfo_value_[index];
      MAKE_INTEGRAL_FRACTIONAL(offset);
      float a = DataType<format>::Decompress(
          buffer_[(write_ptr_ + offset_integral + d.base) & mask_]);
      float b = DataType<format>::Decompress(
          buffer_[(write_ptr_ + offset_integral + d.base + 1) & mask_]);
      float x = a + (b - a) * offset_fractional;
      previous_read_ = x;
      accumulator_ += x * scale;
    }

   private:
    float accumulator_;
    float previous_read_;
    float lfo_value_[2];
    T* buffer_;
    int32_t write_ptr_;
    int32_t mask_;

    DISALLOW_COPY_AND_ASSIGN(Context);
  };

  // `frequency` in cycles per sample.
  inline void SetLFOFrequency(LFOIndex index, float frequency) {
    lfo_[index].template Init<stmlib::COSINE_OSCILLATOR_APPROXIMATE>(frequency * 32.0f);
  }

  inline void Start(Context* c) {
    --write_ptr_;
    if (write_ptr_ < 0) {
      write_ptr_ += size_;
    }
    c->accumulator_ = 0.0f;
    c->previous_read_ = 0.0f;
    c->buffer_ = buffer_;
    c->write_ptr_ = write_ptr_;
    c->mask_ = mask_;
    if ((write_ptr_ & 31) == 0) {
      c->lfo_value_[0] = lfo_[0].Next();
      c->lfo_value_[1] = lfo_[1].Next();
    } else {
      c->lfo_value_[0] = lfo_[0].value();
      c->lfo_value_[1] = lfo_[1].value();
    }
  }

 private:
  int32_t size_;
  int32_t mask_;
  int32_t write_ptr_;
  T* buffer_;
  stmlib::CosineOscillator lfo_[2];

  DISALLOW_COPY_AND_ASSIGN(FxEngine);
};

}  // namespace elements

#endif  // ELEMENTS_DSP_FX_FX_ENGINE_H_
