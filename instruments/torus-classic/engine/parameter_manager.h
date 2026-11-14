// Parameter targets and their 5 ms smoothing. Adapted from Daisy-Seed
// instruments/physical-modeling/src/parameter_manager.h: the reverb
// parameters (Decay, Wet, ReverbDamping, Diffusion, InputGain), the fixed
// Polyphony entry, the pot filter and the hardware switch debounce are gone;
// Jump() and JumpBend() are new. The smoothing arithmetic of the remaining
// parameters is unchanged. See ../UPSTREAM.md.
#pragma once
#include "config.h"
#include <cmath>
#include <algorithm>
namespace torus_classic {
enum Parameter : unsigned {
    Frequency, Structure, Brightness, Damping, Position, Excitation, Model, Master, Count
};
class ParameterManager {
  public:
    ParameterManager() {
        target_.fill(0.5f);
        target_[Excitation] = 0.8f;
        target_[Model] = 0; target_[Master] = 1;
        current_ = target_;
    }
    void Set(unsigned id, float value) { if(id < Count) target_[id] = Unit(value); }
    // Target and smoothed value at once: no glide from the previous value.
    void Jump(unsigned id, float value) { if(id < Count) target_[id] = current_[id] = Unit(value); }
    float Target(unsigned id) const { return target_[id]; }
    float Get(unsigned id) const { return current_[id]; }
    unsigned DesiredModel() const { return std::min(5u, unsigned(target_[Model] * 6.0f)); }
    void SelectModel(unsigned model) { Set(Model, float(std::min(5u, model)) / 5.0f); }
    void SetBend(unsigned raw) { bend_target_ = Bend(raw); }
    void JumpBend(unsigned raw) { bend_ = bend_target_ = Bend(raw); }
    float PitchOffset() const { return Transpose(current_[Frequency]) + bend_; }
    void Advance() {
        // 5 ms at 4 kHz; snap small residuals so endpoints are exact.
        for(unsigned i = 0; i < Count; ++i) {
            current_[i] += 0.0487705755f * (target_[i] - current_[i]);
            if(std::fabs(target_[i] - current_[i]) < 0.000001f) current_[i] = target_[i];
        }
        bend_ += 0.0487705755f * (bend_target_ - bend_);
    }
  private:
    std::array<float, Count> target_{}, current_{};
    float bend_ = 0, bend_target_ = 0;
};
// Original Torus CV scaler: nearest of 11 chords with +/-0.1 chord hysteresis.
class ChordSelector {
  public:
    int Process(float structure) {
        const float chord = Unit(structure) * 10.0f;
        const float hysteresis = chord > chord_ ? -0.1f : 0.1f;
        chord_ = std::clamp(int(chord + hysteresis + 0.5f), 0, 10);
        return chord_;
    }
  private:
    int chord_ = 0;
};
}
