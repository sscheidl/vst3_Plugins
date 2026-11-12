// Everything the plug-in adds to the firmware sound engine: a pitch offset
// (master pitch plus pitch bend), a volume in front of the limiter, and a guard
// for pitches the firmware never had to handle. Free of VST3 types.
#pragma once

#include "toy_piano.h"

#include <cstdint>

namespace taureon::toy_piano_classic {

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
};

} // namespace taureon::toy_piano_classic
