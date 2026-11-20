#include "engine.h"

#include <cmath>
#include <cstddef>

namespace taureon::toy_piano_generative {
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

float HarmonicWeight(float increment) {
    if(increment <= kHarmonicFadeStart)
        return toy_piano::kSecondHarmonic; // exactly the firmware's weight
    if(increment >= toy_piano::kMaxHarmonicIncrement)
        return 0.0f;
    return toy_piano::kSecondHarmonic * (toy_piano::kMaxHarmonicIncrement - increment)
           / (toy_piano::kMaxHarmonicIncrement - kHarmonicFadeStart);
}

double TuningRatio(int note, double amount) {
    if(!(amount > 0.0))
        return 1.0;
    const double exact = 440.0 * std::pow(2.0, (note - 69) / 12.0);
    // The largest whole number below the exact frequency. A frequency that
    // is whole already, as every A from 55 Hz up, loses a full hertz.
    const double whole = std::ceil(exact) - 1.0;
    if(!(whole > 0.0))
        return 1.0;
    return 1.0 - (amount < 1.0 ? amount : 1.0) * (exact - whole) / exact;
}

void Engine::Init(float sample_rate) {
    piano_.Init(sample_rate);
    tuning_.fill(1.0f);
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

void Engine::SetTuning(double amount) {
    tuning_amount_ = amount;
}

void Engine::NoteOn(std::uint8_t note, std::uint8_t velocity, float attack, float decay) {
    if(note > 127 || velocity == 0 || velocity > 127)
        return; // the engine ignores these as well
    piano_.NoteOn(note, velocity, attack, decay);
    // The voice that took the note: a sounding note has exactly one.
    for(std::size_t i = 0; i < toy_piano::kNumMidiVoices; ++i)
        if(piano_.MidiVoiceNote(i) == note)
            tuning_[i] = static_cast<float>(TuningRatio(note, tuning_amount_));
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
        float ratio = ratio_ * tuning_[i]; // the factor is exactly 1 untuned
        float level = 1.0f;
        if(base * ratio >= kNyquistIncrement) {
            ratio = kHeldIncrement / base;
            level = 0.0f;
        }
        // The firmware switches the harmonic off at a quarter of the sample
        // rate in one step, which clicks when a glide crosses it. Fading it
        // out before that point keeps every glide smooth.
        piano_.Modulate(voice, ratio, level, HarmonicWeight(base * ratio), kLevelRamp);
    }
}

} // namespace taureon::toy_piano_generative
