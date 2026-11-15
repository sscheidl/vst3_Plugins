#include "dry_engine.h"

#include "resources.h"
#include "synth.h"

#include <cstring>
#include <new>

namespace torus_classic {

namespace {

constexpr unsigned kParameterOf[kControlCount] = {Frequency, Structure, Brightness, Damping,
                                                  Position,  Excitation, Master};

unsigned ParameterOf(Control control) {
    const auto index = static_cast<unsigned>(control);
    return index < kControlCount ? kParameterOf[index] : static_cast<unsigned>(Count);
}

} // namespace

// The DSP objects have constructors that leave their members as they are, as
// upstream. The firmware clears their memory before constructing them; so
// does this, which also makes every start identical.
struct DryEngine::Impl {
    alignas(Synth) unsigned char storage[sizeof(Synth)];
    Synth* synth = nullptr;

    void Construct(std::uint32_t seed) {
        if(synth)
            synth->~Synth();
        std::memset(storage, 0, sizeof(storage));
        synth = new(storage) Synth{};
        synth->Init(seed);
    }
    ~Impl() {
        if(synth)
            synth->~Synth();
    }
};

DryEngine::DryEngine() : impl_(std::make_unique<Impl>()) {
    // The one place that prepares the tables all instances share: guarded by
    // std::call_once and never reached from Reset() or Render().
    torus::InitResources();
    impl_->Construct(kDefaultSeed);
}

DryEngine::~DryEngine() = default;

void DryEngine::Reset(std::uint32_t seed) {
    impl_->Construct(seed);
}

bool DryEngine::NoteOn(unsigned note, unsigned velocity) {
    return impl_->synth->NoteOn(note, velocity);
}

void DryEngine::SetControl(Control control, float value) {
    impl_->synth->parameters.Set(ParameterOf(control), value);
}

void DryEngine::SetModel(unsigned model) {
    impl_->synth->parameters.SelectModel(model);
}

void DryEngine::SetPitchBend(unsigned raw14) {
    impl_->synth->parameters.SetBend(raw14);
}

void DryEngine::JumpControl(Control control, float value) {
    impl_->synth->parameters.Jump(ParameterOf(control), value);
}

void DryEngine::JumpModel(unsigned model) {
    impl_->synth->JumpModel(model);
}

void DryEngine::JumpPitchBend(unsigned raw14) {
    impl_->synth->parameters.JumpBend(raw14);
}

void DryEngine::Render(float* left, float* right) {
    impl_->synth->Render(left, right);
}

unsigned DryEngine::Model() const {
    return impl_->synth->ModelIndex();
}

unsigned DryEngine::DesiredModel() const {
    return impl_->synth->parameters.DesiredModel();
}

std::uint32_t DryEngine::TriggerCount() const {
    return impl_->synth->TriggerCount();
}

std::size_t DryEngine::PendingNotes() const {
    return impl_->synth->PendingNotes();
}

std::uint32_t DryEngine::OverflowCount() const {
    return impl_->synth->OverflowCount();
}

float DryEngine::VoiceNote(unsigned voice) const {
    return voice < kVoices ? impl_->synth->VoiceNote(voice) : 0.0f;
}

float DryEngine::ControlValue(Control control) const {
    const unsigned parameter = ParameterOf(control);
    return parameter < Count ? impl_->synth->parameters.Get(parameter) : 0.0f;
}

float DryEngine::PitchOffset() const {
    return impl_->synth->parameters.PitchOffset();
}

std::uint32_t DryEngine::NoiseState() const {
    return impl_->synth->RngState();
}

std::size_t DryEngine::StateBytes() {
    return sizeof(Synth);
}

} // namespace torus_classic
