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
// The sample rate the DSP runs at.

#include "elements/dsp/sample_rate.h"

#include <cmath>

#include "elements/resources.h"

namespace elements {

/* static */
bool SampleRate::Supported(float hz) {
  return hz == 32000.0f || hz == 48000.0f || hz == 96000.0f;
}

bool SampleRate::Init(float hz) {
  if (!Supported(hz)) {
    return false;
  }
  if (hz == hz_) {
    return true;
  }
  hz_ = hz;
  ratio_ = hz / kReferenceSampleRate;
  inv_ratio_ = kReferenceSampleRate / hz;
  reference_ = hz == kReferenceSampleRate;
  block_size_ = static_cast<size_t>(
      static_cast<float>(kReferenceBlockSize) * ratio_ + 0.5f);
  noise_gain_ = reference_ ? 1.0f : sqrtf(ratio_);

  if (reference_) {
    svf_gain_ = lut_approx_svf_gain;
    svf_g_ = lut_approx_svf_g;
    svf_h_ = lut_approx_svf_h;
    return true;
  }

  // The original tables say what the filter is at 32 kHz: g = tan(pi * f),
  // f being the cutoff over the sample rate. The same cutoff in Hz at this
  // rate is f / ratio. r is 2 for these tables, h follows from g and r.
  // The height of a one-sample pulse grows with the rate: a sample is
  // shorter, and what the filter and the resonator respond to is its area.
  const double inv_ratio = static_cast<double>(kReferenceSampleRate) / hz;
  for (size_t i = 0; i < kSvfTableSize; ++i) {
    const double g = tan(atan(static_cast<double>(lut_approx_svf_g[i])) *
        inv_ratio);
    svf_g_table_[i] = static_cast<float>(g);
    svf_h_table_[i] = static_cast<float>(1.0 / (1.0 + 2.0 * g + g * g));
    svf_gain_table_[i] = lut_approx_svf_gain[i] * ratio_;
  }
  svf_gain_ = svf_gain_table_;
  svf_g_ = svf_g_table_;
  svf_h_ = svf_h_table_;
  ComputeMatchedLowPass();
  return true;
}

namespace {

const double kPi = 3.14159265358979323846;

// The frequencies at which the matched low-pass is fitted to the original:
// from 50 Hz to 14.5 kHz in equal ratios, so that a low cutoff is fitted
// where it is. Closer to the original's 16 kHz its response is too small to
// matter.
const int kFitPoints = 28;

struct FitPoint {
  // cos and sin of 1, 2 and 3 times the angle the frequency has at the rate
  // in use, and of 1 times the angle it has at 32 kHz.
  double c1, s1, c2, s2, c3, s3;
  double c32, half_c32;
};

// The value in [low, high] at which `error` is smallest: a scan, then a
// golden section search around its best point.
template<typename Error>
double Minimize(
    double low, double high, int steps, int refinements, const Error& error) {
  double best = low;
  double best_error = error(low);
  const double step = (high - low) / steps;
  for (int i = 1; i <= steps; ++i) {
    const double x = low + step * i;
    const double e = error(x);
    if (e < best_error) {
      best_error = e;
      best = x;
    }
  }
  double a = best - step < low ? low : best - step;
  double b = best + step > high ? high : best + step;
  const double ratio = 0.6180339887498949;
  double c = b - ratio * (b - a);
  double d = a + ratio * (b - a);
  double ec = error(c);
  double ed = error(d);
  for (int i = 0; i < refinements; ++i) {
    if (ec < ed) {
      b = d;
      d = c;
      ed = ec;
      c = b - ratio * (b - a);
      ec = error(c);
    } else {
      a = c;
      c = d;
      ec = ed;
      d = a + ratio * (b - a);
      ed = error(d);
    }
  }
  return 0.5 * (a + b);
}

}  // namespace

void SampleRate::ComputeMatchedLowPass() {
  // One section of the original filter at 32 kHz is
  //   G (1 + z^-1) / (1 - p z^-1),  G = g / (1 + g), p = (1 - g) / (1 + g).
  // Its counterpart at this rate keeps the zero at 16 kHz, as a pair
  //   1 - c z^-1 + z^-2,  c = 2 cos(angle of 16 kHz),
  // and has a pole q in place of p, or for p < 0, where the original rings
  // at 16 kHz, a pair of poles of radius q at 16 kHz. A further zero at beta
  // makes up for the different shapes the two kinds of zero and pole have
  // below 16 kHz. q and beta are chosen, for each cutoff, so that the
  // response in dB is closest to the original's over the fitted frequencies.
  // Where the original lets more through a deviation counts for more: the
  // weight is the original's gain, but at least that of -30 dB.
  const double rate = hz_;
  const double c = 2.0 * cos(2.0 * kPi * 16000.0 / rate);

  FitPoint points[kFitPoints];
  for (int i = 0; i < kFitPoints; ++i) {
    const double f = 50.0 * pow(14500.0 / 50.0,
        static_cast<double>(i) / (kFitPoints - 1));
    const double w = 2.0 * kPi * f / rate;
    const double w32 = 2.0 * kPi * f / 32000.0;
    points[i].c1 = cos(w);
    points[i].s1 = sin(w);
    points[i].c2 = cos(2.0 * w);
    points[i].s2 = sin(2.0 * w);
    points[i].c3 = cos(3.0 * w);
    points[i].s3 = sin(3.0 * w);
    points[i].c32 = cos(w32);
    points[i].half_c32 = cos(0.5 * w32);
  }

  double beta = 0.0;
  for (size_t index = 0; index < kSvfTableSize; ++index) {
    const double g = lut_approx_svf_g[index];
    const double p = (1.0 - g) / (1.0 + g);
    const bool ringing = p < 0.0;
    // The original's gain, squared, at the fitted frequencies.
    double target[kFitPoints];
    double weight[kFitPoints];
    for (int i = 0; i < kFitPoints; ++i) {
      const double numerator = (1.0 - p) * points[i].half_c32;
      target[i] = numerator * numerator /
          (1.0 - 2.0 * p * points[i].c32 + p * p);
      const double gain = sqrt(target[i]);
      weight[i] = gain > 0.0316 ? gain : 0.0316;
    }
    // The squared error in dB of the candidate with pole parameter q and
    // extra zero b.
    const auto error = [&](double q, double b) {
      const double a1 = ringing ? -c * q : -q;
      const double a2 = ringing ? q * q : 0.0;
      const double n1 = -(c + b);
      const double n2 = 1.0 + c * b;
      const double n3 = -b;
      const double dc = (1.0 + a1 + a2) / ((2.0 - c) * (1.0 - b));
      double sum = 0.0;
      for (int i = 0; i < kFitPoints; ++i) {
        const FitPoint& k = points[i];
        const double nr = 1.0 + n1 * k.c1 + n2 * k.c2 + n3 * k.c3;
        const double ni = n1 * k.s1 + n2 * k.s2 + n3 * k.s3;
        const double dr = 1.0 + a1 * k.c1 + a2 * k.c2;
        const double di = a1 * k.s1 + a2 * k.s2;
        const double value = dc * dc * (nr * nr + ni * ni) /
            (dr * dr + di * di);
        const double db = 10.0 * log10(value / target[i]);
        sum += weight[i] * db * db;
      }
      return sum;
    };
    // Each in turn, three times; beta starts from the neighbouring cutoff's,
    // which keeps neighbours close.
    double q = 0.0;
    for (int round = 0; round < 3; ++round) {
      q = Minimize(0.0, 0.9999, 48, 20, [&](double x) {
        return error(x, beta);
      });
      beta = Minimize(-0.6, 0.6, 24, 20, [&](double x) {
        return error(q, x);
      });
    }
    const double a1 = ringing ? -c * q : -q;
    const double a2 = ringing ? q * q : 0.0;
    matched_a1_[index] = static_cast<float>(a1);
    matched_a2_[index] = static_cast<float>(a2);
    matched_n1_[index] = static_cast<float>(-(c + beta));
    matched_n2_[index] = static_cast<float>(1.0 + c * beta);
    matched_n3_[index] = static_cast<float>(-beta);
    matched_b0_[index] = static_cast<float>(
        (1.0 + a1 + a2) / ((2.0 - c) * (1.0 - beta)));
  }
}

float SampleRate::Decay(float factor) const {
  if (reference_) {
    return factor;
  }
  if (factor <= 0.0f) {
    return 0.0f;
  }
  return powf(factor, inv_ratio_);
}

}  // namespace elements
