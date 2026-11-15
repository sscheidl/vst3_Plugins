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
// Reverb.
//
// Modified 2026 for vst3_Plugins (TAUREON): delay lengths, modulation depths,
// modulation rates and the damping low-pass follow the sample rate; the
// numbers in the code are the original ones, for 32 kHz. See UPSTREAM.md.

#ifndef ELEMENTS_DSP_FX_REVERB_H_
#define ELEMENTS_DSP_FX_REVERB_H_

#include "stmlib/stmlib.h"

#include "elements/dsp/fx/fx_engine.h"
#include "elements/dsp/sample_rate.h"

namespace elements {

const size_t kNumReverbDelays = 10;

// Lengths of the delay lines at 32 kHz: 4 input diffusers, then twice two
// all-passes and a delay.
const int32_t kReverbDelayLengths[kNumReverbDelays] = {
  150, 214, 319, 527, 2182, 2690, 4501, 2525, 2197, 6312
};

class Reverb {
 public:
  Reverb() { }
  ~Reverb() { }

  // The number of buffer elements the reverb needs at a sample rate: the
  // smallest power of two that holds its delay lines. 32768 at 32 and
  // 48 kHz, 65536 at 96 kHz.
  static size_t BufferSize(const SampleRate& sample_rate) {
    int32_t lengths[kNumReverbDelays];
    for (size_t i = 0; i < kNumReverbDelays; ++i) {
      lengths[i] = static_cast<int32_t>(
          sample_rate.Samples(static_cast<size_t>(kReverbDelayLengths[i])));
    }
    size_t required = E::Required(lengths, kNumReverbDelays);
    size_t size = 32768;
    while (size < required) {
      size <<= 1;
    }
    return size;
  }

  // `buffer` holds `buffer_size` elements, a power of two and at least
  // BufferSize(). False if it is too small; the reverb is then unusable.
  bool Init(
      const SampleRate* sample_rate,
      uint16_t* buffer,
      size_t buffer_size) {
    sample_rate_ = sample_rate;
    if (buffer_size < BufferSize(*sample_rate) ||
        (buffer_size & (buffer_size - 1)) != 0) {
      return false;
    }
    engine_.Init(buffer, buffer_size);
    int32_t lengths[kNumReverbDelays];
    for (size_t i = 0; i < kNumReverbDelays; ++i) {
      lengths[i] = static_cast<int32_t>(
          sample_rate->Samples(static_cast<size_t>(kReverbDelayLengths[i])));
    }
    if (!engine_.Allocate(delay_, lengths, kNumReverbDelays)) {
      return false;
    }
    engine_.SetLFOFrequency(LFO_1, 0.5f / sample_rate->hz());
    engine_.SetLFOFrequency(LFO_2, 0.3f / sample_rate->hz());

    // Positions and modulation depths inside the delay lines.
    ap1_smear_offset_ = sample_rate->Samples(10.0f);
    ap1_smear_depth_ = sample_rate->Samples(80.0f);
    ap1_smear_write_ = static_cast<int32_t>(
        sample_rate->Samples(static_cast<size_t>(100)));
    del2_offset_ = sample_rate->Samples(6211.0f);
    del2_depth_ = sample_rate->Samples(100.0f);

    lp_setting_ = -1.0f;
    set_lp(0.7f);
    diffusion_ = 0.625f;

    // Not set by the original; it relies on the caller's setters and on
    // zeroed memory.
    amount_ = 0.0f;
    input_gain_ = 0.0f;
    reverb_time_ = 0.0f;
    lp_decay_1_ = 0.0f;
    lp_decay_2_ = 0.0f;
    return true;
  }

  void Process(float* left, float* right, size_t size) {
    // This is the Griesinger topology described in the Dattorro paper
    // (4 AP diffusers on the input, then a loop of 2x 2AP+1Delay).
    // Modulation is applied in the loop of the first diffuser AP for additional
    // smearing; and to the two long delays for a slow shimmer/chorus effect.
    const E::DelayLine& ap1 = delay_[0];
    const E::DelayLine& ap2 = delay_[1];
    const E::DelayLine& ap3 = delay_[2];
    const E::DelayLine& ap4 = delay_[3];
    const E::DelayLine& dap1a = delay_[4];
    const E::DelayLine& dap1b = delay_[5];
    const E::DelayLine& del1 = delay_[6];
    const E::DelayLine& dap2a = delay_[7];
    const E::DelayLine& dap2b = delay_[8];
    const E::DelayLine& del2 = delay_[9];
    E::Context c;

    const float kap = diffusion_;
    const float klp = lp_;
    const float krt = reverb_time_;
    const float amount = amount_;
    const float gain = input_gain_;

    float lp_1 = lp_decay_1_;
    float lp_2 = lp_decay_2_;

    while (size--) {
      float wet;
      float apout = 0.0f;
      engine_.Start(&c);

      // Smear AP1 inside the loop.
      c.Interpolate(ap1, ap1_smear_offset_, LFO_1, ap1_smear_depth_, 1.0f);
      c.Write(ap1, ap1_smear_write_, 0.0f);

      c.Read(*left + *right, gain);

      // Diffuse through 4 allpasses.
      c.Read(ap1 TAIL, kap);
      c.WriteAllPass(ap1, -kap);
      c.Read(ap2 TAIL, kap);
      c.WriteAllPass(ap2, -kap);
      c.Read(ap3 TAIL, kap);
      c.WriteAllPass(ap3, -kap);
      c.Read(ap4 TAIL, kap);
      c.WriteAllPass(ap4, -kap);
      c.Write(apout);

      // Main reverb loop.
      c.Load(apout);
      c.Interpolate(del2, del2_offset_, LFO_2, del2_depth_, krt);
      c.Lp(lp_1, klp);
      c.Read(dap1a TAIL, -kap);
      c.WriteAllPass(dap1a, kap);
      c.Read(dap1b TAIL, kap);
      c.WriteAllPass(dap1b, -kap);
      c.Write(del1, 2.0f);
      c.Write(wet, 0.0f);

      *left += (wet - *left) * amount;

      c.Load(apout);
      // c.Interpolate(del1, 4450.0f, LFO_1, 50.0f, krt);
      c.Read(del1 TAIL, krt);
      c.Lp(lp_2, klp);
      c.Read(dap2a TAIL, kap);
      c.WriteAllPass(dap2a, -kap);
      c.Read(dap2b TAIL, -kap);
      c.WriteAllPass(dap2b, kap);
      c.Write(del2, 2.0f);
      c.Write(wet, 0.0f);

      *right += (wet - *right) * amount;

      ++left;
      ++right;
    }

    lp_decay_1_ = lp_1;
    lp_decay_2_ = lp_2;
  }

  inline void set_amount(float amount) {
    amount_ = amount;
  }

  inline void set_input_gain(float input_gain) {
    input_gain_ = input_gain;
  }

  inline void set_time(float reverb_time) {
    reverb_time_ = reverb_time;
  }

  inline void set_diffusion(float diffusion) {
    diffusion_ = diffusion;
  }

  // The coefficient of the damping low-pass as at 32 kHz.
  inline void set_lp(float lp) {
    if (lp != lp_setting_) {
      lp_setting_ = lp;
      lp_ = sample_rate_->OnePole(lp);
    }
  }

 private:
  typedef FxEngine<FORMAT_16_BIT> E;
  E engine_;
  E::DelayLine delay_[kNumReverbDelays];

  const SampleRate* sample_rate_;

  float ap1_smear_offset_;
  float ap1_smear_depth_;
  int32_t ap1_smear_write_;
  float del2_offset_;
  float del2_depth_;

  float amount_;
  float input_gain_;
  float reverb_time_;
  float diffusion_;
  float lp_setting_;
  float lp_;

  float lp_decay_1_;
  float lp_decay_2_;

  DISALLOW_COPY_AND_ASSIGN(Reverb);
};

}  // namespace elements

#endif  // ELEMENTS_DSP_FX_REVERB_H_
