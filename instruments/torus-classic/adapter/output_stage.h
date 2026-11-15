// The end of the signal chain, at the host's sample rate: a low-pass over the
// whole output, then a limiter. Part of the streaming adapter (layer 3), free
// of Torus and VST3 types. Neither exists in the Daisy firmware.
//
// Low-pass: 12 dB per octave, no resonance (a state-variable filter in its
// trapezoidal form, Q 0.707). Its control runs from 0 (kLowHz) to 1; from
// kTop up the filtered signal gives way to the unfiltered one, and at 1 the
// samples pass exactly as they came.
//
// Limiter: both channels share one gain. A sample above kCeiling pulls the
// gain down at once, so no output sample exceeds kCeiling; the gain then
// recovers over kReleaseSeconds. Below the ceiling the gain is exactly 1 and
// the samples pass exactly as they came. No look-ahead, no delay.
//
// The filter's integrators run all the time, also while the filter is faded
// out: closing it again starts from what the signal is now, never from what
// it was when the filter was last open.
//
// Any input gives finite output. A sample that is not a number or infinite is
// a fault, not a level, and enters as 0; a finite one beyond kInputLimit
// enters as kInputLimit. Neither can reach the integrators.
#pragma once

#include <cstdint>
#include <limits>

namespace torus_classic {

class OutputStage {
  public:
    static constexpr float kCeiling = 0.5f; // -6 dBFS
    static constexpr double kReleaseSeconds = 0.15;
    static constexpr double kLowHz = 100.0;
    static constexpr double kHighHz = 20000.0;
    static constexpr double kTop = 0.98;
    static constexpr double kSmoothingSeconds = 0.005;
    static constexpr float kInputLimit = 1.0e6f; // +120 dBFS
    // The glide lands exactly on its target once it is this close.
    static constexpr double kSnap = 1e-6;

    // Sets the sample rate and resets. Does not allocate.
    void Prepare(double sample_rate);
    // Filter and limiter back to rest; the filter control jumps to its target.
    void Reset();

    // The filter control, 0..1; approached over kSmoothingSeconds.
    void SetFilter(float value);
    // The same at once, without the glide.
    void JumpFilter(float value);
    float FilterTarget() const { return target_; }

    // One stereo sample, in place.
    inline void Process(float& left, float& right);

    // For tests.
    float Gain() const { return gain_; }
    float FilterValue() const { return static_cast<float>(value_); }
    float FilterMix() const { return mix_; }

  private:
    void UpdateCoefficients();
    static float Bounded(float sample) {
        if(sample >= -kInputLimit && sample <= kInputLimit)
            return sample;
        constexpr float kMax = std::numeric_limits<float>::max();
        if(sample > kInputLimit && sample <= kMax)
            return kInputLimit;
        if(sample < -kInputLimit && sample >= -kMax)
            return -kInputLimit;
        return 0.0f; // not a number, or infinite
    }

    double sample_rate_ = 48000.0;
    // Low-pass. The control glides in double: in float, steps near the
    // target become too small to add at high sample rates and the glide
    // stops short of it.
    float target_ = 1.0f;
    double value_ = 1.0, smoothing_ = 1.0;
    float mix_ = 0.0f;                  // 0: unfiltered, 1: filtered
    float g_ = 0.0f, k_ = 0.0f;         // filter coefficients
    float a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
    float state_[2][2] = {};            // per channel: the two integrators
    // Limiter.
    float gain_ = 1.0f, release_ = 0.0f;
};

inline void OutputStage::Process(float& left, float& right) {
    if(value_ != target_) {
        // Lands on the target when close, or when a step no longer changes
        // the value.
        const double next = value_ + (target_ - value_) * smoothing_;
        const double rest = target_ - next;
        value_ = next == value_ || (rest < kSnap && rest > -kSnap) ? target_ : next;
        UpdateCoefficients();
    }
    float* channels[2] = {&left, &right};
    for(int c = 0; c < 2; ++c) {
        const float in = Bounded(*channels[c]);
        const float v3 = in - state_[c][1];
        const float v1 = a1_ * state_[c][0] + a2_ * v3;
        const float v2 = state_[c][1] + a2_ * state_[c][0] + a3_ * v3;
        state_[c][0] = 2.0f * v1 - state_[c][0];
        state_[c][1] = 2.0f * v2 - state_[c][1];
        // Fully open the sample passes exactly as it came.
        *channels[c] = mix_ > 0.0f ? in + (v2 - in) * mix_ : in;
    }
    const float l = left < 0.0f ? -left : left, r = right < 0.0f ? -right : right;
    const float peak = l > r ? l : r;
    if(peak > kCeiling) {
        const float needed = kCeiling / peak;
        if(needed < gain_)
            gain_ = needed;
    }
    if(gain_ < 1.0f) {
        left *= gain_;
        right *= gain_;
        // Recover towards 1 for the next sample, and land on it exactly: close
        // to 1 the step becomes smaller than a float can add, so a gain that
        // no longer moves, or is within a ten-thousandth, is set to 1.
        const float next = gain_ + (1.0f - gain_) * release_;
        gain_ = next > gain_ && next < 0.9999f ? next : 1.0f;
    }
}

} // namespace torus_classic
