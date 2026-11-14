#include "sinc_kernel.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace torus_classic {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kInternalRate = 48000.0;

// Modified Bessel function of the first kind, order 0.
double BesselI0(double x) {
    double sum = 1.0, term = 1.0;
    const double half = x * 0.5;
    for(int k = 1; k < 200; ++k) {
        term *= (half / k) * (half / k);
        sum += term;
        if(term < sum * 1e-17)
            break;
    }
    return sum;
}

} // namespace

bool MakeRateRatio(double host_rate, RateRatio& ratio) {
    if(!(host_rate >= kMinHostRate && host_rate <= kMaxHostRate))
        return false; // also rejects NaN
    const auto den = static_cast<std::uint64_t>(std::llround(host_rate * 10000.0));
    const std::uint64_t num = 480000000ull; // 48000 Hz in the same unit
    const std::uint64_t divisor = std::gcd(num, den);
    ratio.num = num / divisor;
    ratio.den = den / divisor;
    return true;
}

void SincKernel::Design(const RateRatio& ratio) {
    den_ = ratio.den;
    const bool down = ratio.num > ratio.den; // host rate below 48 kHz
    // Half-width in input samples: kZeroCrossings periods of the lower rate.
    half_ = down ? static_cast<int>((SincDesign::kZeroCrossings * ratio.num + ratio.den - 1)
                                    / ratio.den)
                 : SincDesign::kZeroCrossings;
    const double lower = down ? static_cast<double>(ratio.den) / static_cast<double>(ratio.num)
                              : 1.0; // lower rate / 48 kHz
    // Cutoff in cycles per input sample: the middle of the transition band.
    const double cutoff = (0.5 - SincDesign::kTransition * 0.5) * lower;
    stopband_hz_ = 0.5 * lower * kInternalRate;
    passband_hz_ = (0.5 - SincDesign::kTransition) * lower * kInternalRate;

    exact_ = ratio.den <= SincDesign::kMaxExactRows;
    if(exact_) {
        rows_ = static_cast<int>(ratio.den);
        row_scale_ = 0.0;
    } else {
        // A stretched kernel changes more slowly with the position, so fewer
        // rows give the same interpolation error.
        rows_ = std::clamp(static_cast<int>(std::ceil(SincDesign::kInterpolatedRows * lower)), 32,
                           SincDesign::kInterpolatedRows);
        row_scale_ = static_cast<double>(rows_) / static_cast<double>(ratio.den);
    }
    const int taps = Taps();
    const int stored_rows = exact_ ? rows_ : rows_ + 1;
    table_.assign(static_cast<std::size_t>(stored_rows) * taps, 0.0f);

    const double i0_beta = BesselI0(SincDesign::kBeta);
    std::vector<double> row(taps);
    for(int r = 0; r < stored_rows; ++r) {
        const double fraction = static_cast<double>(r) / static_cast<double>(rows_);
        double sum = 0.0;
        for(int t = 0; t < taps; ++t) {
            // Distance from the output position to input sample i + j.
            const double distance = fraction - static_cast<double>(t - Left());
            const double x = distance / static_cast<double>(half_);
            double value = 0.0;
            if(std::fabs(x) < 1.0) {
                const double window =
                    BesselI0(SincDesign::kBeta * std::sqrt(1.0 - x * x)) / i0_beta;
                const double argument = kPi * 2.0 * cutoff * distance;
                const double sinc =
                    std::fabs(argument) < 1e-12 ? 1.0 : std::sin(argument) / argument;
                value = 2.0 * cutoff * sinc * window;
            }
            row[t] = value;
            sum += value;
        }
        // Every position passes DC with gain 1 exactly.
        for(int t = 0; t < taps; ++t)
            table_[static_cast<std::size_t>(r) * taps + t] = static_cast<float>(row[t] / sum);
    }
}

void SincKernel::Coefficients(double fraction, std::vector<double>& out) const {
    const int taps = Taps();
    out.assign(taps, 0.0);
    if(exact_) {
        int r = static_cast<int>(std::llround(fraction * rows_));
        r = std::clamp(r, 0, rows_ - 1);
        for(int t = 0; t < taps; ++t)
            out[t] = table_[static_cast<std::size_t>(r) * taps + t];
        return;
    }
    const double position = std::clamp(fraction, 0.0, 1.0) * rows_;
    int r = std::min(static_cast<int>(position), rows_ - 1);
    const double weight = position - r;
    for(int t = 0; t < taps; ++t) {
        const double a = table_[static_cast<std::size_t>(r) * taps + t];
        const double b = table_[static_cast<std::size_t>(r + 1) * taps + t];
        out[t] = a + (b - a) * weight;
    }
}

} // namespace torus_classic
