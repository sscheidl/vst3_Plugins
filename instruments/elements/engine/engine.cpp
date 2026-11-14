#include "engine.h"

#include "elements/dsp/part.h"
#include "stmlib/dsp/dsp.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace elements_synth {

namespace {

constexpr unsigned Index(Control control) {
    return static_cast<unsigned>(control);
}

float Clamp01(float value) {
    return value >= 0.0f ? (value <= 1.0f ? value : 1.0f) : 0.0f; // NaN becomes 0
}

float Constrain(float value, float low, float high) {
    return value < low ? low : (value > high ? high : value);
}

// The limits the hardware's control reader puts on what it hands to the DSP
// (cv_scaler.cc, BIND).
constexpr float kPotTop = 0.9995f;
constexpr float kStrikeTimbreTop = 0.995f;
// SPACE at this value is what a control voltage on top of the pot reaches:
// the reverb freezes from 1.75 on.
constexpr float kSpaceFrozen = 2.0f;
// The second smoothing of the control reader, per control.
constexpr float kMetaSmoothing = 0.05f;
constexpr float kSlowSmoothing = 0.01f;

} // namespace

struct Engine::Impl {
    elements::Part part;
    std::vector<std::uint16_t> reverb_buffer;
    bool prepared = false;
    double rate = 0.0;
    int quantum = 0;

    // Controls: where they are set, the hardware's first smoothing of them,
    // and its second one for the five controls that have it.
    float target[kControlCount] = {};
    float pot[kControlCount] = {};
    float blow_meta = 0.0f, strike_meta = 0.0f, geometry = 0.0f, position = 0.0f, space = 0.0f;
    float volume = 1.0f, volume_step = 0.0f;
    int volume_ramp_left = 0;

    // Keys held down, oldest first.
    struct Key {
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
    };
    Key held[kHeldNotes] = {};
    int held_count = 0;
    int sounding_note = -1;
    unsigned sounding_velocity = 64;
    bool gate = false;           // a key holds the gate open
    bool gap_pending = false;    // one quantum with the gate closed comes first
    bool strike_pending = false; // then at least one with the gate open
    bool last_gate = false;      // what the DSP saw in the last quantum

    float bend_target = 0.0f, bend = 0.0f;
    float pitch = 69.0f;
    float strength = 0.5f;

    std::uint32_t seed = kDefaultSeed;
    std::uint64_t restarts = 0;
    std::uint64_t quanta = 0;
    bool inject_fault = false;

    float main[kMaxQuantum] = {};
    float aux[kMaxQuantum] = {};
    float silence[kMaxQuantum] = {};

    float SpaceTarget() const {
        return ControlIsOn(target[Index(Control::Freeze)]) ? kSpaceFrozen
                                                            : pot[Index(Control::Space)];
    }

    void StartDsp() {
        part.Init(static_cast<float>(rate), reverb_buffer.data(), reverb_buffer.size());
        part.mutable_rng()->Seed(seed);
    }

    void SettleControls() {
        for(unsigned i = 0; i < kControlCount; ++i)
            pot[i] = target[i];
        blow_meta = Constrain(pot[Index(Control::BlowFlow)], 0.0f, kPotTop);
        strike_meta = Constrain(pot[Index(Control::StrikeMallet)], 0.0f, kPotTop);
        geometry = Constrain(pot[Index(Control::Geometry)], 0.0f, kPotTop);
        position = Constrain(pot[Index(Control::Position)], 0.0f, kPotTop);
        space = Constrain(SpaceTarget(), 0.0f, kSpaceFrozen);
        volume = target[Index(Control::Volume)];
        volume_step = 0.0f;
        volume_ramp_left = 0;
    }

    void ReleaseKeys() {
        held_count = 0;
        gate = false;
        gap_pending = false;
        strike_pending = false;
    }

    void RemoveKey(unsigned note) {
        int kept = 0;
        for(int i = 0; i < held_count; ++i)
            if(held[i].note != note)
                held[kept++] = held[i];
        held_count = kept;
    }

    // One step of the control reader, then the patch as the DSP gets it.
    void UpdatePatch() {
        // Every pot is low-passed, as the hardware does once per block.
        for(unsigned i = 0; i < kControlCount; ++i)
            pot[i] += kPotSmoothing * (target[i] - pot[i]);

        elements::Patch* patch = part.mutable_patch();
        patch->exciter_envelope_shape = pot[Index(Control::Contour)];
        patch->exciter_bow_level = pot[Index(Control::BowLevel)];
        patch->exciter_blow_level = pot[Index(Control::BlowLevel)];
        patch->exciter_strike_level = pot[Index(Control::StrikeLevel)];

        patch->exciter_bow_timbre = Constrain(pot[Index(Control::BowTimbre)], 0.0f, kPotTop);
        blow_meta += (Constrain(pot[Index(Control::BlowFlow)], 0.0f, kPotTop) - blow_meta)
                     * kMetaSmoothing;
        patch->exciter_blow_meta = blow_meta;
        patch->exciter_blow_timbre = Constrain(pot[Index(Control::BlowTimbre)], 0.0f, kPotTop);
        strike_meta += (Constrain(pot[Index(Control::StrikeMallet)], 0.0f, kPotTop) - strike_meta)
                       * kMetaSmoothing;
        patch->exciter_strike_meta = strike_meta;
        patch->exciter_strike_timbre =
            Constrain(pot[Index(Control::StrikeTimbre)], 0.0f, kStrikeTimbreTop);
        geometry += (Constrain(pot[Index(Control::Geometry)], 0.0f, kPotTop) - geometry)
                    * kMetaSmoothing;
        patch->resonator_geometry = geometry;
        patch->resonator_brightness = Constrain(pot[Index(Control::Brightness)], 0.0f, kPotTop);
        patch->resonator_damping = Constrain(pot[Index(Control::Damping)], 0.0f, kPotTop);
        position += (Constrain(pot[Index(Control::Position)], 0.0f, kPotTop) - position)
                    * kSlowSmoothing;
        patch->resonator_position = position;
        space += (Constrain(SpaceTarget(), 0.0f, kSpaceFrozen) - space) * kSlowSmoothing;
        patch->space = space;
    }
};

Engine::Engine() : impl_(std::make_unique<Impl>()) {
    for(unsigned i = 0; i < kControlCount; ++i)
        impl_->target[i] = static_cast<float>(kControlDefaults[i]);
    impl_->SettleControls();
}

Engine::~Engine() = default;

bool Engine::SupportedRate(double sample_rate) {
    return sample_rate == 48000.0 || sample_rate == 96000.0 || sample_rate == 32000.0;
}

bool Engine::Prepare(double sample_rate) {
    if(!SupportedRate(sample_rate))
        return false;
    const std::size_t size = elements::Part::ReverbBufferSize(static_cast<float>(sample_rate));
    if(size == 0)
        return false;
    Impl& s = *impl_;
    s.reverb_buffer.assign(size, 0);
    s.rate = sample_rate;
    s.prepared = true;
    Reset(s.seed);
    s.quantum = static_cast<int>(s.part.block_size());
    return true;
}

bool Engine::Prepared() const {
    return impl_->prepared;
}

double Engine::SampleRate() const {
    return impl_->rate;
}

int Engine::Quantum() const {
    return impl_->quantum;
}

void Engine::Reset(std::uint32_t seed) {
    Impl& s = *impl_;
    s.seed = seed;
    if(s.prepared)
        s.StartDsp();
    s.SettleControls();
    s.ReleaseKeys();
    s.sounding_note = -1;
    s.sounding_velocity = 64;
    s.last_gate = false;
    s.bend_target = s.bend = 0.0f;
    s.pitch = 69.0f;
    s.strength = 0.5f;
    s.quanta = 0;
    s.inject_fault = false;
}

void Engine::SetControl(Control control, float value) {
    const unsigned index = Index(control);
    if(index >= kControlCount)
        return;
    Impl& s = *impl_;
    value = Clamp01(value);
    if(control == Control::Volume && value != s.target[index]) {
        const int samples = kVolumeRampQuanta * (s.quantum > 0 ? s.quantum : 1);
        s.volume_step = (value - s.volume) / static_cast<float>(samples);
        s.volume_ramp_left = samples;
    }
    s.target[index] = value;
}

void Engine::JumpControl(Control control, float value) {
    const unsigned index = Index(control);
    if(index >= kControlCount)
        return;
    Impl& s = *impl_;
    s.target[index] = Clamp01(value);
    s.pot[index] = s.target[index];
    switch(control) {
        case Control::BlowFlow: s.blow_meta = Constrain(s.pot[index], 0.0f, kPotTop); break;
        case Control::StrikeMallet: s.strike_meta = Constrain(s.pot[index], 0.0f, kPotTop); break;
        case Control::Geometry: s.geometry = Constrain(s.pot[index], 0.0f, kPotTop); break;
        case Control::Position: s.position = Constrain(s.pot[index], 0.0f, kPotTop); break;
        case Control::Space:
        case Control::Freeze: s.space = Constrain(s.SpaceTarget(), 0.0f, kSpaceFrozen); break;
        case Control::Volume:
            s.volume = s.target[index];
            s.volume_step = 0.0f;
            s.volume_ramp_left = 0;
            break;
        default: break;
    }
}

float Engine::ControlTarget(Control control) const {
    const unsigned index = Index(control);
    return index < kControlCount ? impl_->target[index] : 0.0f;
}

void Engine::NoteOn(unsigned note, unsigned velocity) {
    if(note > 127 || velocity == 0 || velocity > 127)
        return;
    Impl& s = *impl_;
    s.RemoveKey(note);
    if(s.held_count == kHeldNotes) { // the oldest key is forgotten
        for(int i = 1; i < kHeldNotes; ++i)
            s.held[i - 1] = s.held[i];
        --s.held_count;
    }
    s.held[s.held_count].note = static_cast<std::uint8_t>(note);
    s.held[s.held_count].velocity = static_cast<std::uint8_t>(velocity);
    ++s.held_count;
    s.sounding_note = static_cast<int>(note);
    s.sounding_velocity = velocity;
    s.gate = true;
    // Every key strikes: the DSP needs to see the gate closed in between.
    s.gap_pending = s.last_gate;
    s.strike_pending = true;
}

void Engine::NoteOff(unsigned note) {
    if(note > 127)
        return;
    Impl& s = *impl_;
    s.RemoveKey(note);
    if(static_cast<int>(note) != s.sounding_note || !s.gate)
        return;
    if(s.held_count > 0) {
        // The most recent key still held takes over; no new strike.
        s.sounding_note = s.held[s.held_count - 1].note;
        s.sounding_velocity = s.held[s.held_count - 1].velocity;
    } else {
        s.gate = false;
    }
}

void Engine::AllNotesOff() {
    impl_->ReleaseKeys();
}

void Engine::SetPitchBend(float semitones) {
    if(std::isfinite(semitones))
        impl_->bend_target = Constrain(semitones, -24.0f, 24.0f);
}

void Engine::JumpPitchBend(float semitones) {
    SetPitchBend(semitones);
    impl_->bend = impl_->bend_target;
}

void Engine::Render(float* left, float* right) {
    Impl& s = *impl_;
    const int n = s.quantum;
    if(!s.prepared || n <= 0)
        return;

    s.UpdatePatch();

    // The gate of this quantum. A key that went down since the last one gets
    // a closed quantum first if the gate was open, then an open one even if
    // the key is up again already.
    bool gate = s.gate;
    if(s.gap_pending) {
        gate = false;
        s.gap_pending = false;
    } else if(s.strike_pending) {
        gate = true;
        s.strike_pending = false;
    }
    s.last_gate = gate;

    s.bend += kBendSmoothing * (s.bend_target - s.bend);
    s.pitch = static_cast<float>(s.sounding_note >= 0 ? s.sounding_note : 69)
              + static_cast<float>(CoarseSemitones(s.target[Index(Control::Coarse)]))
              + static_cast<float>(FineSemitones(s.pot[Index(Control::Fine)]));
    s.strength = static_cast<float>(
        StrengthFor(s.sounding_velocity, s.target[Index(Control::Velocity)]));

    elements::PerformanceState performance;
    performance.gate = gate;
    performance.note = s.pitch;
    performance.modulation = s.bend;
    performance.strength = s.strength;
    s.part.Process(performance, s.silence, s.silence, s.main, s.aux, static_cast<size_t>(n));
    ++s.quanta;

    if(s.inject_fault) {
        s.inject_fault = false;
        s.main[0] = std::numeric_limits<float>::quiet_NaN();
    }
    bool finite = true;
    for(int i = 0; i < n; ++i)
        finite = finite && std::isfinite(s.main[i]) && std::isfinite(s.aux[i]);
    if(!finite) {
        // Should not happen. If it does, a filter or the reverb holds a value
        // it cannot leave on its own: silence, the DSP from scratch, keys up.
        s.StartDsp();
        s.ReleaseKeys();
        s.last_gate = false;
        ++s.restarts;
        std::fill(s.main, s.main + n, 0.0f);
        std::fill(s.aux, s.aux + n, 0.0f);
    }

    // The hardware's outputs: `aux` is the left one, `main` the right one,
    // each through its converter (stmlib::SoftConvert, without the rounding
    // to 16 bits): half the level, limited softly, then clipped at full scale.
    for(int i = 0; i < n; ++i) {
        if(s.volume_ramp_left > 0) {
            s.volume += s.volume_step;
            if(--s.volume_ramp_left == 0)
                s.volume = s.target[Index(Control::Volume)];
        }
        const float l = Constrain(stmlib::SoftLimit(s.aux[i] * 0.5f), -1.0f, 1.0f);
        const float r = Constrain(stmlib::SoftLimit(s.main[i] * 0.5f), -1.0f, 1.0f);
        left[i] = l * s.volume;
        right[i] = r * s.volume;
    }
}

bool Engine::Gate() const {
    return impl_->last_gate;
}

int Engine::SoundingNote() const {
    return impl_->sounding_note;
}

int Engine::HeldCount() const {
    return impl_->held_count;
}

float Engine::Strength() const {
    return impl_->strength;
}

float Engine::Pitch() const {
    return impl_->pitch;
}

float Engine::ExciterLevel() const {
    return impl_->prepared ? impl_->part.exciter_level() : 0.0f;
}

float Engine::ResonatorLevel() const {
    return impl_->prepared ? impl_->part.resonator_level() : 0.0f;
}

std::uint64_t Engine::RestartCount() const {
    return impl_->restarts;
}

std::uint64_t Engine::RenderedQuanta() const {
    return impl_->quanta;
}

std::size_t Engine::StateBytes() {
    return sizeof(Impl);
}

void Engine::InjectFaultForTest() {
    impl_->inject_fault = true;
}

} // namespace elements_synth
