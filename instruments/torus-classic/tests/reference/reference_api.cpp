#include "reference_api.h"

#include "physical_modeling.h"
#include "stmlib/utils/random.h"

#include <cstring>
#include <new>

namespace torus_classic_reference {

struct FirmwareSynth::Impl {
    alignas(physical_modeling::Synth) unsigned char storage[sizeof(physical_modeling::Synth)];
    physical_modeling::Synth* synth = nullptr;
};

FirmwareSynth::FirmwareSynth() : impl_(new Impl) {
    Reset(0x21);
}

FirmwareSynth::~FirmwareSynth() {
    if(impl_->synth)
        impl_->synth->~Synth();
    delete impl_;
}

void FirmwareSynth::Reset(std::uint32_t seed) {
    if(impl_->synth)
        impl_->synth->~Synth();
    std::memset(impl_->storage, 0, sizeof(impl_->storage));
    impl_->synth = new(impl_->storage) physical_modeling::Synth{};
    impl_->synth->Init();
    stmlib::Random::Seed(seed);
}

bool FirmwareSynth::NoteOn(unsigned note, unsigned velocity) {
    return impl_->synth->NoteOn(note, velocity);
}

void FirmwareSynth::Set(unsigned parameter, float value) {
    impl_->synth->parameters.Set(parameter, value);
}

void FirmwareSynth::SelectModel(unsigned model) {
    impl_->synth->parameters.SelectModel(model);
}

void FirmwareSynth::SetBend(unsigned raw14) {
    impl_->synth->parameters.SetBend(raw14);
}

void FirmwareSynth::Render(float* left, float* right) {
    impl_->synth->Process(left, right, physical_modeling::kQuantum);
}

unsigned FirmwareSynth::Model() const {
    return impl_->synth->ModelIndex();
}

float FirmwareSynth::Current(unsigned parameter) const {
    return impl_->synth->parameters.Get(parameter);
}

std::uint32_t FirmwareSynth::TriggerCount() const {
    return impl_->synth->TriggerCount();
}

std::uint32_t FirmwareSynth::RandomState() {
    return stmlib::Random::state();
}

std::size_t FirmwareSynth::StateBytes() {
    return sizeof(physical_modeling::Synth);
}

} // namespace torus_classic_reference
