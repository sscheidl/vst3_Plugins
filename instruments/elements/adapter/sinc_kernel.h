// Taken unchanged from Torus Classic 0.2.0 (adapter/sinc_kernel.*), apart from
// the namespace.
//
// The interpolation filter of the streaming adapter: a Kaiser-windowed sinc,
// tabulated per fractional position. Pure arithmetic, free of engine and VST3
// types. Built outside the audio thread; read-only afterwards.
#pragma once

#include <cstdint>
#include <vector>

namespace elements_synth {

// The conversion from the engine's 48 kHz to the host rate, as an exact
// ratio: `num` internal samples pass per `den` host samples.
struct RateRatio {
    std::uint64_t num = 1, den = 1;
    bool Unity() const { return num == den; }
};

// Rates are taken to 1/10000 Hz; integer rates are exact. The range covers
// every rate Steinberg's validator tries (1234.5678 Hz to 1234567.8 Hz).
constexpr double kMinHostRate = 1000.0;
constexpr double kMaxHostRate = 4000000.0;
bool MakeRateRatio(double host_rate, RateRatio& ratio);

struct SincDesign {
    // Zero crossings of the sinc on each side, counted at the lower of the
    // two rates. 48 gives 96 taps when the host rate is 48 kHz or above.
    static constexpr int kZeroCrossings = 48;
    // Kaiser window for a nominal 100 dB stopband.
    static constexpr double kStopbandDb = 100.0;
    static constexpr double kBeta = 0.1102 * (kStopbandDb - 8.7);
    // Transition band of that window, as a fraction of the lower rate. The
    // stopband begins at half the lower rate; the passband ends this far below.
    static constexpr double kTransition = (kStopbandDb - 7.95) / (14.36 * 2 * kZeroCrossings);
    // Up to this many fractional positions get their own exact row. Rates
    // with more positions use kInterpolatedRows rows and interpolate linearly.
    static constexpr std::uint64_t kMaxExactRows = 2048;
    static constexpr int kInterpolatedRows = 1024;
};

class SincKernel {
  public:
    // Allocates and fills the table. Not for the audio thread.
    void Design(const RateRatio& ratio);

    // Input samples used left and right of an output position: indices
    // i - Left() .. i + Right() around the position i + fraction.
    int Left() const { return half_ - 1; }
    int Right() const { return half_; }
    int Taps() const { return 2 * half_; }
    int Rows() const { return rows_; }
    bool Exact() const { return exact_; }
    std::size_t TableBytes() const { return table_.size() * sizeof(float); }
    // Edges in Hz for the host rate the kernel was designed for.
    double PassbandEdgeHz() const { return passband_hz_; }
    double StopbandEdgeHz() const { return stopband_hz_; }

    // One output sample of each channel. `left` and `right` point at the input
    // sample with index i - Left(); the position is i + rem / den.
    inline void Interpolate(const float* left, const float* right, std::uint64_t rem,
                            float& out_left, float& out_right) const;

    // The coefficients at fractional position `fraction` (0..1), as
    // Interpolate() uses them. For tests.
    void Coefficients(double fraction, std::vector<double>& out) const;

  private:
    static inline void Dot(const float* left, const float* right, const float* c, int taps,
                           float& out_left, float& out_right);

    int half_ = 0;
    int rows_ = 0;
    bool exact_ = true;
    std::uint64_t den_ = 1;
    double row_scale_ = 0.0; // rows per unit of rem, interpolated tables
    double passband_hz_ = 0.0, stopband_hz_ = 0.0;
    std::vector<float> table_; // rows_ (+1 when interpolated) x Taps()
};

inline void SincKernel::Dot(const float* left, const float* right, const float* c, int taps,
                            float& out_left, float& out_right) {
    // Four partial sums per channel: the order of additions is fixed in the
    // source, so the result does not depend on the compiler's vectoriser.
    float l0 = 0.0f, l1 = 0.0f, l2 = 0.0f, l3 = 0.0f;
    float r0 = 0.0f, r1 = 0.0f, r2 = 0.0f, r3 = 0.0f;
    int k = 0;
    for(; k + 4 <= taps; k += 4) {
        l0 += left[k] * c[k];
        r0 += right[k] * c[k];
        l1 += left[k + 1] * c[k + 1];
        r1 += right[k + 1] * c[k + 1];
        l2 += left[k + 2] * c[k + 2];
        r2 += right[k + 2] * c[k + 2];
        l3 += left[k + 3] * c[k + 3];
        r3 += right[k + 3] * c[k + 3];
    }
    for(; k < taps; ++k) {
        l0 += left[k] * c[k];
        r0 += right[k] * c[k];
    }
    out_left = (l0 + l1) + (l2 + l3);
    out_right = (r0 + r1) + (r2 + r3);
}

inline void SincKernel::Interpolate(const float* left, const float* right, std::uint64_t rem,
                                    float& out_left, float& out_right) const {
    const int taps = Taps();
    if(exact_) {
        Dot(left, right, table_.data() + static_cast<std::size_t>(rem) * taps, taps, out_left,
            out_right);
        return;
    }
    const double position = static_cast<double>(rem) * row_scale_;
    int row = static_cast<int>(position);
    if(row >= rows_)
        row = rows_ - 1;
    const float weight = static_cast<float>(position - row);
    float l0, r0, l1, r1;
    const float* c = table_.data() + static_cast<std::size_t>(row) * taps;
    Dot(left, right, c, taps, l0, r0);
    Dot(left, right, c + taps, taps, l1, r1);
    out_left = l0 + (l1 - l0) * weight;
    out_right = r0 + (r1 - r0) * weight;
}

} // namespace elements_synth
