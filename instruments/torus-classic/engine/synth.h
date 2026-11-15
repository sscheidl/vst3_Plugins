// The dry four-voice instrument: note FIFO, voice triggering, model changes
// and the output stage around torus::Part. Adapted from Daisy-Seed
// instruments/physical-modeling/src/physical_modeling.h. There is no reverb
// here: no instance, no buffer, no parameters. See ../UPSTREAM.md.
#pragma once
#include "parameter_manager.h"
#include "audio_output.h"
#include "dsp/part.h"
namespace torus_classic {
class Synth {
  public:
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
    ParameterManager parameters;
    unsigned ModelIndex() const { return unsigned(part_.model()); }
    uint32_t TriggerCount() const { return part_.triggers(); }
    float VoiceNote(unsigned voice) const { return part_.voice_note(voice); }
    size_t PendingNotes() const { return write_ - read_; }
    uint32_t OverflowCount() const { return overflow_; }
    uint32_t RngState() const { return part_.rng_state(); }
    static constexpr size_t kNoteCapacity = 256;
  private:
    struct Note { uint8_t note, velocity; };
    torus::Part part_{};
    std::array<Note, kNoteCapacity> notes_{};
    uint32_t write_ = 0, read_ = 0, overflow_ = 0;
    float fade_ = 1;
    bool fading_out_ = false;
    ChordSelector chord_{};
};
}
