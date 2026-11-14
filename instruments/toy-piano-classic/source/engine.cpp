#include "engine.h"

#include <cmath>
#include <cstddef>

namespace taureon::toy_piano_classic {
namespace {

// The pitch is recomputed every 16 samples while it glides.
constexpr std::uint32_t kGlideInterval = 16;
constexpr float kGlideTimeConstant = 0.005f; // seconds
constexpr float kGlideSnap = 0.001f;         // semitones

// The volume follows its target per sample, without zipper noise.
constexpr float kVolumeTimeConstant = 0.010f; // seconds
constexpr float kVolumeSnap = 1e-5f;

// Phase increment at the Nyquist frequency. The firmware plays at most note 127
// at 48 kHz (0.26). A plug-in can be asked for more: two octaves up, or a low
// host sample rate. From an increment of 1 on, the engine's phase wrap would
// run past its sine table, so such voices are held just below Nyquist and muted.
constexpr float kNyquistIncrement = 0.5f;
constexpr float kHeldIncrement = 0.49f;

// Samples over which a voice fades when it is muted or comes back.
constexpr std::size_t kLevelRamp = 64;

// One step of an exponential approach. Lands exactly on the target once it is
// within `snap`, or once the step has become too small to change a float:
// close to the target the step can round away to nothing, and the value would
// stop just short of it for good.
float Approach(float current, float target, float coefficient, float snap) {
    const float next = current + coefficient * (target - current);
    return next == current || std::fabs(target - next) < snap ? target : next;
}

} // namespace

void Engine::Init(float sample_rate) {
    piano_.Init(sample_rate);
    glide_coefficient_ =
        1.0f - std::exp(-static_cast<float>(kGlideInterval) / (kGlideTimeConstant * sample_rate));
    volume_coefficient_ = 1.0f - std::exp(-1.0f / (kVolumeTimeConstant * sample_rate));
    pitch_current_ = pitch_target_;
    ratio_ = std::pow(2.0f, pitch_current_ / 12.0f);
    volume_current_ = volume_target_;
    tick_ = 0;
}

void Engine::SetPitch(float semitones) {
    pitch_target_ = semitones;
}

void Engine::SetVolume(float gain) {
    volume_target_ = gain;
}

void Engine::NoteOn(std::uint8_t note, std::uint8_t velocity, float attack, float decay) {
    piano_.NoteOn(note, velocity, attack, decay);
    // NoteOn sets the struck voice to the note's own pitch; transpose it again.
    ApplyPitch();
}

float Engine::Process() {
    if(pitch_current_ != pitch_target_ && ++tick_ >= kGlideInterval) {
        tick_ = 0;
        StepGlide();
    }
    if(volume_current_ != volume_target_)
        volume_current_ = Approach(volume_current_, volume_target_, volume_coefficient_, kVolumeSnap);
    // At volume 1 the product is the raw sum itself, bit for bit.
    return toy_piano::ToyPiano::SoftLimit(piano_.ProcessRaw() * volume_current_);
}

void Engine::StepGlide() {
    pitch_current_ = Approach(pitch_current_, pitch_target_, glide_coefficient_, kGlideSnap);
    ratio_ = std::pow(2.0f, pitch_current_ / 12.0f); // exactly 1 at 0 semitones
    ApplyPitch();
}

void Engine::ApplyPitch() {
    // The firmware's modulation interface carries the pitch offset. A plug-in
    // only uses the MIDI pool; the 16 key voices in front of it stay silent.
    for(std::size_t i = 0; i < toy_piano::kNumMidiVoices; ++i) {
        const std::size_t voice = toy_piano::kNumKeys + i;
        const float base = piano_.BaseIncrement(voice);
        float ratio = ratio_;
        float level = 1.0f;
        if(base * ratio >= kNyquistIncrement) {
            ratio = kHeldIncrement / base;
            level = 0.0f;
        }
        // Modulate() drops the second harmonic by itself once it would alias.
        piano_.Modulate(voice, ratio, level, toy_piano::kSecondHarmonic, kLevelRamp);
    }
}

} // namespace taureon::toy_piano_classic
