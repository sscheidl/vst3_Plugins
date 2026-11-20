// Everything the plug-in adds to the firmware sound engine: a pitch offset
// (master pitch plus pitch bend), a volume in front of the limiter, a guard
// for pitches the firmware never had to handle, a fade of the second
// harmonic below its cut-off, and a tuning that can pull every note down
// towards whole hertz. Free of VST3 types.
#pragma once

#include "toy_piano.h"

#include <array>
#include <cstdint>

namespace taureon::toy_piano_generative {

// The second harmonic fades out linearly between these two phase increments
// (0.20 and 0.25 of the sample rate: 9.6 and 12 kHz at 48 kHz). Below, it has
// the firmware's 12 %; from 0.25 on, where it would alias, it is gone, as in
// the firmware. The firmware switches at 0.25 in one step instead.
constexpr float kHarmonicFadeStart = 0.20f;
float HarmonicWeight(float increment);

// The tuning. At amount 0 every note has its exact equal-tempered frequency
// (A4 = 440 Hz). At amount 1 it sounds at the next whole hertz below that:
// A4 at 439 Hz, A1 at 54 Hz, which is 32 cents flat. In between, the pitch
// moves evenly from one to the other. Returns the factor on the exact
// frequency; exactly 1 at amount 0.
double TuningRatio(int note, double amount);

class Engine {
  public:
    // Not real-time safe (builds the sine table). Takes the pitch and volume
    // that are set at this moment without a glide.
    void Init(float sample_rate);
    // Offset in semitones, fractions allowed. Glides there within a few
    // milliseconds; sounding notes follow.
    void SetPitch(float semitones);
    // Linear gain applied to the sum of all voices before the soft limiter, so
    // the output stays below the firmware's ceiling at any setting.
    void SetVolume(float gain);
    // The tuning amount, 0 to 1 (see TuningRatio). Applies from the next
    // strike; notes that sound keep the tuning they were struck with.
    void SetTuning(double amount);
    // Times in seconds, latched for this strike as in the firmware.
    void NoteOn(std::uint8_t note, std::uint8_t velocity, float attack, float decay);
    // One mono sample. With pitch 0 and volume 1 this is exactly
    // ToyPiano::Process().
    float Process();

    float PitchRatio() const { return ratio_; }
    float Volume() const { return volume_current_; }
    const toy_piano::ToyPiano& Piano() const { return piano_; }

  private:
    void StepGlide();
    void ApplyPitch();

    toy_piano::ToyPiano piano_;
    float pitch_target_ = 0.0f;  // semitones
    float pitch_current_ = 0.0f; // semitones
    float ratio_ = 1.0f;
    float glide_coefficient_ = 1.0f;
    std::uint32_t tick_ = 0;
    float volume_target_ = 1.0f;
    float volume_current_ = 1.0f;
    float volume_coefficient_ = 1.0f;
    double tuning_amount_ = 0.0;
    // Per MIDI voice: the tuning factor of the note it was last struck with.
    std::array<float, toy_piano::kNumMidiVoices> tuning_{};
};

} // namespace taureon::toy_piano_generative
