// Layer 2 of Torus Classic: the dry instrument engine behind a small
// interface. Free of Torus, stmlib and VST3 types, so that the layers above
// (streaming adapter, plug-in) do not see the DSP sources or their macros.
//
// Component id "torus-classic.engine", version kEngineVersion.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

namespace torus_classic {

constexpr const char* kEngineVersion = "0.2.1";

// Continuous controls, each 0..1. A value set here is a target; the engine
// approaches it with the firmware's 5 ms smoothing.
enum class Control : unsigned {
    Tune,       // -24..+24 semitones, 0.5 is no transposition
    Structure,
    Brightness,
    Damping,
    Position,
    Excitation, // multiplied by the velocity at the strike
    Volume,     // the firmware's master, in front of the output limiter
};
constexpr unsigned kControlCount = 7;

class DryEngine {
  public:
    static constexpr double kSampleRate = 48000.0;
    static constexpr int kQuantum = 12; // samples per Render()
    // Voices come in banks of four: one bank is the firmware's instrument,
    // further banks are more of the same (see synth.h). All of them exist
    // from the constructor on; the number in use can change at any time.
    static constexpr int kVoices = 4;    // per bank
    static constexpr int kMaxBanks = 4;
    static constexpr int kMaxVoices = kVoices * kMaxBanks;
    static constexpr int kModels = 6;
    // A bank beyond the first whose output stays below kQuietLevel (-180
    // dBFS, in front of Volume) for kQuietQuanta (1 s) is put away and costs
    // nothing until its next strike, see RenderedBanks().
    static constexpr int kQuietQuanta = 4000;
    static constexpr float kQuietLevel = 1e-9f;
    static constexpr int kNoteCapacity = 256; // strikes waiting for a quantum
    // The firmware's noise generator starts here after power-on.
    static constexpr std::uint32_t kDefaultSeed = 0x21;
    static constexpr unsigned kBendCentre = 8192; // 14-bit pitch wheel

    // Allocates the DSP memory and prepares the tables all instances share.
    // Not for the audio thread.
    DryEngine();
    ~DryEngine();
    DryEngine(const DryEngine&) = delete;
    DryEngine& operator=(const DryEngine&) = delete;

    // Back to the state after power-on: silence, no waiting notes, defaults,
    // model 0, the noise source at `seed`. Does not allocate or lock.
    void Reset(std::uint32_t seed = kDefaultSeed);

    // A strike for the next quantum. At most four per bank start per quantum
    // (four with four voices, sixteen with sixteen); more wait their turn.
    // False if the note or velocity is invalid (velocity 0 included) or
    // kNoteCapacity strikes already wait.
    bool NoteOn(unsigned note, unsigned velocity);

    void SetControl(Control control, float value);
    void SetModel(unsigned model);       // 0..5; the change fades out and in
    void SetPitchBend(unsigned raw14);   // 0..16383, +/-2 semitones, smoothed
    // The number of voices: kVoices, 2 * kVoices, ... kMaxVoices; anything
    // else counts as the next lower of these, at least kVoices. Takes effect
    // with the next quantum. More voices: sounding notes ring on. Fewer: the
    // notes of the banks that stay ring on, the others fade out within 4 ms.
    void SetPolyphony(unsigned voices);

    // The same without smoothing or fade: for a start from a known state.
    void JumpControl(Control control, float value);
    void JumpModel(unsigned model);
    void JumpPitchBend(unsigned raw14);
    void JumpPolyphony(unsigned voices); // after Reset(), before the first strike

    // The next kQuantum samples, each channel within +/-0.8.
    void Render(float* left, float* right);

    // For tests and diagnostics.
    unsigned Model() const;
    unsigned DesiredModel() const;
    std::uint32_t TriggerCount() const;
    std::size_t PendingNotes() const;
    std::uint32_t OverflowCount() const;
    float VoiceNote(unsigned voice) const; // voice 0..kMaxVoices-1, bank by bank
    unsigned Polyphony() const;            // voices, as set
    unsigned RenderedBanks() const;        // the first, and the others while they sound
    float ControlValue(Control control) const; // the smoothed value
    float ControlTarget(Control control) const; // where it is heading
    float PitchOffset() const;                 // semitones, tune plus bend
    std::uint32_t NoiseState(unsigned bank = 0) const;
    static std::size_t StateBytes(); // DSP memory of one instance

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace torus_classic
