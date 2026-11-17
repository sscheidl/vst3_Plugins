// Messier's own backend for the vocoder FFT declared in fft.h. Not a copy from
// Daisy-Seed: the firmware links CMSIS-DSP (fft_cmsis.cpp), which this repository
// does not contain. MIT, (c) 2026 TAUREON, like the rest of this repository.
//
// The same contract as the firmware's backend: a real 2048-point FFT in the Csound
// packing [DC, Nyquist, re1, im1, ..., re1023, im1023], the forward transform
// unscaled with the kernel e^(-j 2 pi k n / N), the inverse scaled so that
// Inverse(Forward(x)) == x. Computed as one complex 1024-point radix-2 FFT of the
// even and odd samples and a split into the real spectrum.
//
// Thread safety: the tables are constants, built once on first use (a function-local
// static, which C++ initialises exactly once). Forward and Inverse keep no state of
// their own; they work in the caller's buffers. Instances in several threads share
// nothing they write.
#include "fft.h"

#include <cmath>
#include <cstdint>

namespace neb::fft {
namespace {

constexpr int kN = 2048;        // real length, FFT_N in reference_dsp.h
constexpr int kM = kN / 2;      // complex length
constexpr int kLogM = 10;
static_assert((1 << kLogM) == kM);

struct Tables {
    float cos_m[kM / 2], sin_m[kM / 2]; // e^(-j 2 pi k / M) = cos - j sin
    float cos_n[kM], sin_n[kM];         // e^(-j 2 pi k / N) = cos - j sin
    std::uint16_t reverse[kM];

    Tables() {
        constexpr double kPi = 3.14159265358979323846;
        for(int k = 0; k < kM / 2; ++k) {
            cos_m[k] = static_cast<float>(std::cos(2.0 * kPi * k / kM));
            sin_m[k] = static_cast<float>(std::sin(2.0 * kPi * k / kM));
        }
        for(int k = 0; k < kM; ++k) {
            cos_n[k] = static_cast<float>(std::cos(2.0 * kPi * k / kN));
            sin_n[k] = static_cast<float>(std::sin(2.0 * kPi * k / kN));
        }
        for(int i = 0; i < kM; ++i) {
            int r = 0;
            for(int b = 0, x = i; b < kLogM; ++b, x >>= 1)
                r = (r << 1) | (x & 1);
            reverse[i] = static_cast<std::uint16_t>(r);
        }
    }
};

const Tables& Get() {
    static const Tables tables;
    return tables;
}

// In place on kM interleaved complex values. `inverse` conjugates the kernel; no
// scaling here.
void Complex(float* z, bool inverse, const Tables& t) {
    for(int i = 0; i < kM; ++i) {
        const int j = t.reverse[i];
        if(j > i) {
            float* a = z + 2 * i;
            float* b = z + 2 * j;
            const float re = a[0], im = a[1];
            a[0] = b[0];
            a[1] = b[1];
            b[0] = re;
            b[1] = im;
        }
    }
    const float sign = inverse ? 1.0f : -1.0f;
    for(int length = 2; length <= kM; length <<= 1) {
        const int half = length >> 1, step = kM / length;
        for(int start = 0; start < kM; start += length) {
            for(int j = 0, k = 0; j < half; ++j, k += step) {
                const float wr = t.cos_m[k], wi = sign * t.sin_m[k];
                float* u = z + 2 * (start + j);
                float* v = z + 2 * (start + j + half);
                const float tr = v[0] * wr - v[1] * wi;
                const float ti = v[0] * wi + v[1] * wr;
                v[0] = u[0] - tr;
                v[1] = u[1] - ti;
                u[0] += tr;
                u[1] += ti;
            }
        }
    }
}

} // namespace

void Init() {
    Get();
}

void Forward(float* time, float* spectrum) {
    const Tables& t = Get();
    // z[n] = x[2n] + j x[2n+1] is the time buffer as it lies in memory.
    Complex(time, false, t);
    const float* z = time;
    spectrum[0] = z[0] + z[1]; // DC
    spectrum[1] = z[0] - z[1]; // Nyquist
    for(int k = 1; k < kM; ++k) {
        const float ar = z[2 * k], ai = z[2 * k + 1];               // Z[k]
        const float br = z[2 * (kM - k)], bi = -z[2 * (kM - k) + 1]; // conj Z[M-k]
        const float er = 0.5f * (ar + br), ei = 0.5f * (ai + bi);  // even part E[k]
        const float or_ = 0.5f * (ai - bi), oi = -0.5f * (ar - br); // odd part O[k] = (Z - conj)/(2j)
        const float wr = t.cos_n[k], wi = -t.sin_n[k];               // W^k
        spectrum[2 * k] = er + (wr * or_ - wi * oi);
        spectrum[2 * k + 1] = ei + (wr * oi + wi * or_);
    }
}

void Inverse(float* spectrum, float* time) {
    const Tables& t = Get();
    const float* x = spectrum;
    time[0] = 0.5f * (x[0] + x[1]);
    time[1] = 0.5f * (x[0] - x[1]);
    for(int k = 1; k < kM; ++k) {
        const float ar = x[2 * k], ai = x[2 * k + 1];               // X[k]
        const float br = x[2 * (kM - k)], bi = -x[2 * (kM - k) + 1]; // conj X[M-k] = X[M+k]
        const float er = 0.5f * (ar + br), ei = 0.5f * (ai + bi);  // E[k]
        const float dr = 0.5f * (ar - br), di = 0.5f * (ai - bi);
        const float wr = t.cos_n[k], wi = t.sin_n[k];                // W^-k
        const float or_ = dr * wr - di * wi, oi = dr * wi + di * wr; // O[k]
        time[2 * k] = er - oi; // Z[k] = E[k] + j O[k]
        time[2 * k + 1] = ei + or_;
    }
    Complex(time, true, t);
    constexpr float kScale = 1.0f / kM;
    for(int i = 0; i < kN; ++i)
        time[i] *= kScale;
}

} // namespace neb::fft
