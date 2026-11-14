#include "output_stage.h"

#include <algorithm>
#include <cmath>

namespace torus_classic {

namespace {
constexpr double kPi = 3.14159265358979323846;
} // namespace

void OutputStage::Prepare(double sample_rate) {
    sample_rate_ = sample_rate > 0.0 ? sample_rate : 48000.0;
    smoothing_ = static_cast<float>(1.0 - std::exp(-1.0 / (kSmoothingSeconds * sample_rate_)));
    release_ = static_cast<float>(1.0 - std::exp(-1.0 / (kReleaseSeconds * sample_rate_)));
    Reset();
}

void OutputStage::Reset() {
    value_ = target_;
    for(auto& channel : state_)
        channel[0] = channel[1] = 0.0f;
    gain_ = 1.0f;
    UpdateCoefficients();
}

void OutputStage::SetFilter(float value) {
    target_ = value >= 0.0f ? (value <= 1.0f ? value : 1.0f) : 0.0f; // NaN becomes 0
}

void OutputStage::JumpFilter(float value) {
    SetFilter(value);
    value_ = target_;
    UpdateCoefficients();
}

void OutputStage::UpdateCoefficients() {
    const double value = value_;
    // Above kTop: 20 kHz, fading into the unfiltered signal.
    mix_ = static_cast<float>(std::clamp((1.0 - value) / (1.0 - kTop), 0.0, 1.0));
    const double position = std::min(value / kTop, 1.0);
    double hz = kLowHz * std::pow(kHighHz / kLowHz, position);
    hz = std::min(hz, 0.45 * sample_rate_); // keep clear of half the sample rate
    const double g = std::tan(kPi * hz / sample_rate_);
    const double k = 1.0 / 0.70710678118654752; // 1/Q, no resonance
    const double a1 = 1.0 / (1.0 + g * (g + k));
    g_ = static_cast<float>(g);
    k_ = static_cast<float>(k);
    a1_ = static_cast<float>(a1);
    a2_ = static_cast<float>(g * a1);
    a3_ = static_cast<float>(g * g * a1);
}

} // namespace torus_classic
