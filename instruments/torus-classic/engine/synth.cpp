// Adapted from Daisy-Seed instruments/physical-modeling/src/physical_modeling.cpp.
// The dry path is the firmware's, statement for statement; the reverb send,
// the reverb itself and the dry/wet mix are removed. See ../UPSTREAM.md.
// The banks beyond the first are this port's own; see synth.h.
#include "synth.h"
#include <cstring>
#include <new>
namespace torus_classic {
namespace {
// Every sample below the level. Not a number is not below anything.
bool Quiet(const float* left, const float* right) {
    for(size_t i = 0; i < kQuantum; ++i)
        if(!(std::fabs(left[i]) < Synth::kQuietLevel && std::fabs(right[i]) < Synth::kQuietLevel)) return false;
    return true;
}
}
void Synth::Init(uint32_t seed) {
    // The firmware fills the sine table here. In the plug-in DryEngine's
    // constructor has done that, once for all instances; Init() can run on
    // the audio thread and must not touch shared state.
    seed_ = seed;
    for(size_t b = 0; b < kMaxBanks; ++b) {
        part_[b].Init();
        part_[b].Seed(BankSeed(seed, b));
        bank_[b] = Bank{};
    }
    // The first bank is the firmware's Part and always runs.
    bank_[0].state = BankState::On;
    banks_ = banks_wanted_ = 1; strike_ = 0; triggers_ = 0;
    parameters = ParameterManager{};
    write_ = read_ = overflow_ = 0; fade_ = 1; fading_out_ = false; chord_ = ChordSelector{};
}
void Synth::ResetBank(size_t bank) {
    // As DryEngine does for the whole instrument: the DSP objects' constructors
    // leave their members as they are, so the memory is cleared first.
    const torus::ResonatorModel model = part_[0].model();
    torus::Part* part = &part_[bank];
    part->~Part();
    std::memset(static_cast<void*>(part), 0, sizeof(torus::Part));
    part = new(part) torus::Part();
    part->Init();
    part->Seed(BankSeed(seed_, bank));
    part->set_model(model);
    bank_[bank] = Bank{};
}
bool Synth::NoteOn(unsigned note, unsigned velocity) {
    if(note > 127 || velocity > 127 || velocity == 0) return false;
    if(write_ - read_ == notes_.size()) { ++overflow_; return false; }
    notes_[write_++ % notes_.size()] = {uint8_t(note), uint8_t(velocity)};
    return true;
}
void Synth::JumpModel(unsigned model) {
    parameters.Jump(Model, float(std::min(5u, model)) / 5.0f);
    for(torus::Part& part : part_) part.set_model(static_cast<torus::ResonatorModel>(parameters.DesiredModel()));
    fade_ = 1; fading_out_ = false;
}
unsigned Synth::RenderedBanks() const {
    unsigned count = 0;
    for(const Bank& bank : bank_) count += bank.state != BankState::Off;
    return count;
}
void Synth::Render(float* left, float* right) {
    parameters.Advance();
    const unsigned desired = parameters.DesiredModel();
    if(desired != ModelIndex()) fading_out_ = true;
    if(fading_out_ && fade_ == 0) {
        for(torus::Part& part : part_) part.set_model(static_cast<torus::ResonatorModel>(desired));
        fading_out_ = false;
    }
    if(banks_ != banks_wanted_) {
        // More banks: the next strikes go to the new ones, which nothing has
        // struck yet (before the first strike of all there is no difference,
        // and the first bank comes first). Fewer: the banks left out take no
        // more strikes and fade out.
        if(banks_wanted_ > banks_ && triggers_ > 0) strike_ = kVoices * banks_;
        banks_ = banks_wanted_;
        if(strike_ >= kVoices * banks_) strike_ = 0;
        for(size_t b = banks_; b < kMaxBanks; ++b)
            if(bank_[b].state == BankState::On) bank_[b].state = BankState::Leaving;
    }
    // At most four triggers per bank before each render; never overwrite an unrendered strike.
    if(!fading_out_) for(size_t i = 0; i < kVoices * banks_ && read_ != write_; ++i) {
        const size_t b = strike_ / kVoices;
        // A bank still fading out after the number of voices went down and up
        // again: the strike waits the few quanta until it is back.
        if(bank_[b].state == BankState::Leaving) break;
        const Note note = notes_[read_++ % notes_.size()];
        part_[b].Trigger(float(note.note), float(note.velocity) / 127.0f * parameters.Get(Excitation));
        bank_[b].state = BankState::On;
        bank_[b].quiet = 0;
        strike_ = (strike_ + 1) % (kVoices * banks_);
        ++triggers_;
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
    part_[0].Process(state, patch, input, left, right, kQuantum);
    for(size_t b = 1; b < kMaxBanks; ++b) {
        Bank& bank = bank_[b];
        if(bank.state == BankState::Off) continue;
        float bank_left[kQuantum], bank_right[kQuantum];
        part_[b].Process(state, patch, input, bank_left, bank_right, kQuantum);
        bank.quiet = Quiet(bank_left, bank_right) ? bank.quiet + 1 : 0;
        for(size_t i = 0; i < kQuantum; ++i) {
            if(bank.state == BankState::Leaving) bank.gain = std::max(0.0f, bank.gain - 1.0f/192.0f);
            left[i] += bank_left[i] * bank.gain; right[i] += bank_right[i] * bank.gain;
        }
        if(bank.quiet >= kQuietQuanta || (bank.state == BankState::Leaving && bank.gain == 0)) ResetBank(b);
    }
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
