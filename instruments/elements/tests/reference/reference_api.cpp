// Compiled with the namespaces `elements` and `stmlib` renamed by macro (see
// ../../CMakeLists.txt), like the reference sources themselves.
#include "reference_api.h"

#include "elements/dsp/part.h"
#include "elements/resources.h"
#include "stmlib/utils/random.h"

#include <cstring>
#include <new>

namespace elements_reference_api {

struct OriginalPart::Impl {
    // Raw memory: the part is constructed into it after it has been cleared.
    alignas(elements::Part) unsigned char storage[sizeof(elements::Part)];
    std::uint16_t reverb_buffer[32768];
    float silence[kBlockSize];
    elements::Part* part;
};

OriginalPart::OriginalPart() : impl_(new Impl) {
    impl_->part = nullptr;
    Reset();
}

OriginalPart::~OriginalPart() {
    delete impl_;
}

void OriginalPart::Reset(std::uint32_t seed) {
    // The original leaves several members to the zeroed memory of the
    // hardware's start-up (panic_, easter_egg_, the tube, phases).
    std::memset(impl_->storage, 0, sizeof(impl_->storage));
    std::memset(impl_->reverb_buffer, 0, sizeof(impl_->reverb_buffer));
    std::memset(impl_->silence, 0, sizeof(impl_->silence));
    impl_->part = new (impl_->storage) elements::Part();
    impl_->part->Init(impl_->reverb_buffer);
    stmlib::Random::Seed(seed);
}

Patch OriginalPart::GetPatch() const {
    const elements::Patch& p = *impl_->part->mutable_patch();
    Patch patch;
    patch.exciter_envelope_shape = p.exciter_envelope_shape;
    patch.exciter_bow_level = p.exciter_bow_level;
    patch.exciter_bow_timbre = p.exciter_bow_timbre;
    patch.exciter_blow_level = p.exciter_blow_level;
    patch.exciter_blow_meta = p.exciter_blow_meta;
    patch.exciter_blow_timbre = p.exciter_blow_timbre;
    patch.exciter_strike_level = p.exciter_strike_level;
    patch.exciter_strike_meta = p.exciter_strike_meta;
    patch.exciter_strike_timbre = p.exciter_strike_timbre;
    patch.exciter_signature = p.exciter_signature;
    patch.resonator_geometry = p.resonator_geometry;
    patch.resonator_brightness = p.resonator_brightness;
    patch.resonator_damping = p.resonator_damping;
    patch.resonator_position = p.resonator_position;
    patch.resonator_modulation_frequency = p.resonator_modulation_frequency;
    patch.resonator_modulation_offset = p.resonator_modulation_offset;
    patch.reverb_diffusion = p.reverb_diffusion;
    patch.reverb_lp = p.reverb_lp;
    patch.space = p.space;
    return patch;
}

void OriginalPart::SetPatch(const Patch& patch) {
    elements::Patch& p = *impl_->part->mutable_patch();
    p.exciter_envelope_shape = patch.exciter_envelope_shape;
    p.exciter_bow_level = patch.exciter_bow_level;
    p.exciter_bow_timbre = patch.exciter_bow_timbre;
    p.exciter_blow_level = patch.exciter_blow_level;
    p.exciter_blow_meta = patch.exciter_blow_meta;
    p.exciter_blow_timbre = patch.exciter_blow_timbre;
    p.exciter_strike_level = patch.exciter_strike_level;
    p.exciter_strike_meta = patch.exciter_strike_meta;
    p.exciter_strike_timbre = patch.exciter_strike_timbre;
    p.exciter_signature = patch.exciter_signature;
    p.resonator_geometry = patch.resonator_geometry;
    p.resonator_brightness = patch.resonator_brightness;
    p.resonator_damping = patch.resonator_damping;
    p.resonator_position = patch.resonator_position;
    p.resonator_modulation_frequency = patch.resonator_modulation_frequency;
    p.resonator_modulation_offset = patch.resonator_modulation_offset;
    p.reverb_diffusion = patch.reverb_diffusion;
    p.reverb_lp = patch.reverb_lp;
    p.space = patch.space;
}

void OriginalPart::Process(bool gate, float note, float modulation, float strength, float* main,
                           float* aux) {
    elements::PerformanceState state;
    state.gate = gate;
    state.note = note;
    state.modulation = modulation;
    state.strength = strength;
    impl_->part->Process(state, impl_->silence, impl_->silence, main, aux, kBlockSize);
}

std::uint32_t OriginalPart::RandomState() {
    return stmlib::Random::state();
}

std::size_t OriginalPart::StateBytes() {
    return sizeof(Impl);
}

const float* OriginalSvfG() {
    return elements::lut_approx_svf_g;
}

const float* OriginalSvfH() {
    return elements::lut_approx_svf_h;
}

const float* OriginalSvfGain() {
    return elements::lut_approx_svf_gain;
}

} // namespace elements_reference_api
