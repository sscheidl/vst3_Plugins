// The dry instrument: note FIFO, voice triggering, model changes and the
// output stage around torus::Part. Adapted from Daisy-Seed
// instruments/physical-modeling/src/physical_modeling.h. There is no reverb
// here: no instance, no buffer, no parameters. See ../UPSTREAM.md.
//
// Since 0.2.0 of the engine: more than four voices, as banks. A bank is one
// unchanged four-voice torus::Part; the firmware has exactly one, and so has
// this with one bank, statement for statement. Further banks are rendered
// with the same patch and summed in front of the fade and the output stage.
//
//   Strikes fill the banks in turn, four at a time: strikes 0..3 go to bank
//   0, 4..7 to bank 1, and so on round. Inside a bank the Part takes its
//   voices in turn as it always did. So a strike on a full instrument takes
//   the voice that was struck longest ago, and left and right alternate from
//   strike to strike as with four voices. When the number of banks goes up,
//   the next strikes go to the new banks first. That order holds as long as
//   the number of voices stays put; right after it has gone up, a strike can
//   take a voice struck later than one in another bank, because each Part
//   keeps its own turn among its four voices (a strike always takes a voice,
//   never one whose strike has not been rendered yet).
//
//   A bank beyond the first is rendered only while it sounds: from its first
//   strike until its own output has stayed below kQuietLevel for
//   kQuietQuanta. Then it is put back into the state it had at Init(), noise
//   source included. A bank that the number of voices no longer includes
//   fades out over 4 ms and is put back the same way; the banks that stay
//   ring on. The first bank always runs, as the firmware's does, so the
//   output of the instrument with four voices is the firmware's, bit for bit.
//
//   Every bank has its own noise source, started from the instance's seed
//   and the bank's number.
//
// "Sounds" is a level, not exact zero: a resonator never arrives at zero.
// The modal filters and the FM envelope settle some 750 dB down, a few of the
// smallest numbers a float holds, and stay there. kQuietLevel is -180 dBFS,
// 36 dB below what a 24-bit converter resolves.
#pragma once
#include "parameter_manager.h"
#include "audio_output.h"
#include "dsp/part.h"
namespace torus_classic {
class Synth {
  public:
    static constexpr size_t kMaxBanks = 4;
    // A second without a sample of kQuietLevel or more: longer than anything
    // inside a Part can hold a signal back (its longest delay line is 2048
    // samples). The level is that of a Part's output, in front of the fade,
    // Volume and the output stage, which only make it smaller.
    static constexpr uint32_t kQuietQuanta = 4000;
    static constexpr float kQuietLevel = 1e-9f;
    // Everything back to the state after power-on. `seed` starts this
    // instance's noise source. Does not allocate or lock; expects the shared
    // tables to exist (torus::InitResources, called by DryEngine's constructor).
    void Init(uint32_t seed);
    bool NoteOn(unsigned note, unsigned velocity);
    // Renders exactly kQuantum samples.
    void Render(float* left, float* right);
    // Selects a model at once, without the fade a running change gets. For a
    // start from a known state; Init() leaves model 0.
    void JumpModel(unsigned model);
    // The number of banks, 1..kMaxBanks, from the next Render() on.
    void SetBanks(unsigned banks) { banks_wanted_ = std::clamp<unsigned>(banks, 1, kMaxBanks); }
    // The same at once, strikes starting at bank 0 again: after Init().
    void JumpBanks(unsigned banks) { SetBanks(banks); banks_ = banks_wanted_; strike_ = 0; }
    ParameterManager parameters;
    unsigned ModelIndex() const { return unsigned(part_[0].model()); }
    unsigned Banks() const { return banks_wanted_; }
    // Banks that are being rendered: the first always, the others while they sound.
    unsigned RenderedBanks() const;
    uint32_t TriggerCount() const { return triggers_; }
    float VoiceNote(unsigned voice) const { return part_[voice / kVoices].voice_note(voice % kVoices); }
    size_t PendingNotes() const { return write_ - read_; }
    uint32_t OverflowCount() const { return overflow_; }
    uint32_t RngState(unsigned bank = 0) const { return part_[bank].rng_state(); }
    static uint32_t BankSeed(uint32_t seed, size_t bank) { return seed + uint32_t(bank) * 0x9E3779B9u; }
    static constexpr size_t kNoteCapacity = 256;
  private:
    struct Note { uint8_t note, velocity; };
    enum class BankState : uint8_t { Off, On, Leaving };
    struct Bank {
        BankState state = BankState::Off;
        float gain = 1;      // below 1 only while leaving
        uint32_t quiet = 0;  // quanta below kQuietLevel in a row
    };
    // The bank as Init() leaves it, at the current model. Does not allocate.
    void ResetBank(size_t bank);
    torus::Part part_[kMaxBanks]{};
    Bank bank_[kMaxBanks]{};
    unsigned banks_ = 1, banks_wanted_ = 1;
    unsigned strike_ = 0; // the next strike's place among the kVoices * banks_ voices
    uint32_t seed_ = 0, triggers_ = 0;
    std::array<Note, kNoteCapacity> notes_{};
    uint32_t write_ = 0, read_ = 0, overflow_ = 0;
    float fade_ = 1;
    bool fading_out_ = false;
    ChordSelector chord_{};
};
}
