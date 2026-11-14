// Layer 2 of the Elements instrument: the modal synthesis DSP (layer 1, the
// adapted Mutable Instruments code in ../core) made playable from notes and
// controls. Free of plug-in framework and DSP types, so that it can serve a
// VST3 shell here and firmware elsewhere.
//
// What it adds to the DSP:
//  * a monophonic voice with last-note priority: one gate, one pitch;
//  * the smoothing of the pots that the hardware does in its control reader;
//  * key velocity as the hardware's STRENGTH, the pitch wheel as its FM input;
//  * the output conversion of the hardware, without the 16-bit rounding, and
//    a volume;
//  * a guard that restarts the DSP should it ever produce a non-finite sample.
//
// The engine renders quanta of 0.5 ms: Quantum() samples at SampleRate().
// Everything is meant for one thread; Prepare() allocates, nothing else does.
#pragma once

#include "controls.h"

#include <cstdint>
#include <memory>

namespace elements_synth {

constexpr const char* kEngineVersion = "0.1.0";

class Engine {
  public:
    // The largest quantum: 48 samples at 96 kHz.
    static constexpr int kMaxQuantum = 48;
    // Notes held down at once that are remembered for last-note priority.
    static constexpr int kHeldNotes = 16;
    static constexpr std::uint32_t kDefaultSeed = 0x21; // of the exciters' random generator
    // The pots glide like the hardware's: this much of the way per quantum.
    static constexpr float kPotSmoothing = 0.01f;
    // The pitch wheel, like the hardware's FM input.
    static constexpr float kBendSmoothing = 0.5f;
    // The volume reaches its value over this many quanta, linearly.
    static constexpr int kVolumeRampQuanta = 10;

    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // True for 48000 (the rate the instrument is made for), 96000 and 32000
    // (the rate of the hardware, for comparisons with the original code).
    static bool SupportedRate(double sample_rate);

    // Sets the sample rate, allocates and resets. False for an unsupported
    // rate; the engine then stays as it was. Not for the audio thread.
    bool Prepare(double sample_rate);
    bool Prepared() const;
    double SampleRate() const;
    // Samples per Render() call: 24 at 48 kHz, 48 at 96 kHz, 16 at 32 kHz.
    int Quantum() const;

    // Silence: the DSP from scratch, no key held, the wheel centred, every
    // control at its target, the random generator at `seed`. Does not
    // allocate.
    void Reset(std::uint32_t seed = kDefaultSeed);

    // A control, 0..1 (see controls.h). It glides to the value as the
    // hardware's pots do; Coarse, Freeze and Velocity act at the next quantum.
    void SetControl(Control control, float value);
    // The same at once, without a glide: for a start from a known state.
    void JumpControl(Control control, float value);
    float ControlTarget(Control control) const;

    // A key goes down, velocity 1..127. It becomes the sounding note; if the
    // gate is already open it closes for one quantum, so that every key
    // strikes. Other notes and velocities are ignored.
    void NoteOn(unsigned note, unsigned velocity);
    // A key goes up. If it was the sounding one, the most recent key still
    // held takes over without a new strike; with none left the gate closes.
    void NoteOff(unsigned note);
    void AllNotesOff();
    // The pitch wheel, in semitones.
    void SetPitchBend(float semitones);
    void JumpPitchBend(float semitones);

    // The next Quantum() samples of each channel.
    void Render(float* left, float* right);

    // For tests and diagnostics.
    bool Gate() const;              // as the DSP last saw it
    int SoundingNote() const;       // the key that sets the pitch, -1 before the first
    int HeldCount() const;
    float Strength() const;
    float Pitch() const;            // of the last quantum: MIDI note with tuning, without the wheel
    float ExciterLevel() const;     // the hardware's two meters, 0..1
    float ResonatorLevel() const;
    std::uint64_t RestartCount() const; // times the guard had to restart the DSP
    std::uint64_t RenderedQuanta() const;
    static std::size_t StateBytes();

    // For tests only: makes the next Render() see a non-finite sample.
    void InjectFaultForTest();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace elements_synth
