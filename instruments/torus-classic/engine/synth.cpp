// Adapted from Daisy-Seed instruments/physical-modeling/src/physical_modeling.cpp.
// The dry path is the firmware's, statement for statement; the reverb send,
// the reverb itself and the dry/wet mix are removed. See ../UPSTREAM.md.
#include "synth.h"
namespace torus_classic {
void Synth::Init(uint32_t seed) {
    // The firmware fills the sine table here. In the plug-in DryEngine's
    // constructor has done that, once for all instances; Init() can run on
    // the audio thread and must not touch shared state.
    part_.Init();
    part_.Seed(seed);
    parameters = ParameterManager{};
    write_ = read_ = overflow_ = 0; fade_ = 1; fading_out_ = false; chord_ = ChordSelector{};
}
bool Synth::NoteOn(unsigned note, unsigned velocity) {
    if(note > 127 || velocity > 127 || velocity == 0) return false;
    if(write_ - read_ == notes_.size()) { ++overflow_; return false; }
    notes_[write_++ % notes_.size()] = {uint8_t(note), uint8_t(velocity)};
    return true;
}
void Synth::JumpModel(unsigned model) {
    parameters.Jump(Model, float(std::min(5u, model)) / 5.0f);
    part_.set_model(static_cast<torus::ResonatorModel>(parameters.DesiredModel()));
    fade_ = 1; fading_out_ = false;
}
void Synth::Render(float* left, float* right) {
    parameters.Advance();
    const unsigned desired = parameters.DesiredModel();
    if(desired != ModelIndex()) fading_out_ = true;
    if(fading_out_ && fade_ == 0) {
        part_.set_model(static_cast<torus::ResonatorModel>(desired));
        fading_out_ = false;
    }
    // At most four triggers before each render; never overwrite an unrendered strike.
    if(!fading_out_) for(size_t i = 0; i < kVoices && read_ != write_; ++i) {
        const Note note = notes_[read_++ % notes_.size()];
        part_.Trigger(float(note.note), float(note.velocity) / 127.0f * parameters.Get(Excitation));
    }
    torus::Patch patch{};
    patch.structure = parameters.Get(Structure);
    patch.brightness = parameters.Get(Brightness);
    patch.damping = parameters.Get(Damping);
    patch.position = parameters.Get(Position);
    torus::PerformanceState state{};
    state.internal_exciter = true;
    state.tonic = 0;
    state.fm = parameters.PitchOffset();
    state.chord = chord_.Process(patch.structure);
    float input[kQuantum]{};
    part_.Process(state, patch, input, left, right, kQuantum);
    for(size_t i = 0; i < kQuantum; ++i) {
        fade_ = fading_out_ ? std::max(0.0f, fade_ - 1.0f/192.0f) : std::min(1.0f, fade_ + 1.0f/192.0f);
        left[i] *= fade_; right[i] *= fade_;
    }
    for(size_t i = 0; i < kQuantum; ++i) {
        left[i] = Output(left[i], parameters.Get(Master));
        // Model 5: the firmware inverts the dry right channel here, after its
        // reverb send. Kept; Part mixes the two channels by Position before.
        const float dry_right = ModelIndex() == 5 ? -right[i] : right[i];
        right[i] = Output(dry_right, parameters.Get(Master));
    }
}
}
