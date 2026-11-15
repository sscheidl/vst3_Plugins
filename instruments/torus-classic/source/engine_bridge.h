// Connects the dry engine (layer 2) to the streaming adapter (layer 3): the
// adapter's events become engine calls. Free of VST3 types, so the tests can
// use the same connection the plug-in uses.
//
// One thing is decided here rather than in the engine: Tune sounds in whole
// semitones unless free tuning is on. Both arrive as events and are combined
// when their quantum comes, so the result does not depend on the order in
// which a host block delivered them. The semitone itself is not rounded
// here: a Tune event (TuneEvent) carries the step TuneStep() found in the
// host's double value, beside the value for free tuning.
#pragma once

#include "dry_engine.h"
#include "parameters.h"
#include "stream_adapter.h"

namespace torus_classic {

// The control index of the free-tuning switch among the adapter's control
// events, after the engine's own controls.
constexpr unsigned kTuneFreeControl = kControlCount;

class EngineBridge final : public QuantumEngine {
  public:
    explicit EngineBridge(DryEngine& engine) : engine_(engine) {}

    void HandleEvent(const EngineEvent& event) override {
        switch(event.type) {
            case EngineEvent::kNoteOn: engine_.NoteOn(event.index, event.data); break;
            case EngineEvent::kControl:
                if(event.index == static_cast<unsigned>(Control::Tune)) {
                    tune_ = event.value;
                    tune_step_ = static_cast<int>(event.data) - kTuneStepOffset;
                    engine_.SetControl(Control::Tune, SoundingTune());
                } else if(event.index == kTuneFreeControl) {
                    tune_free_ = event.value >= 0.5f;
                    engine_.SetControl(Control::Tune, SoundingTune());
                } else if(event.index < kControlCount) {
                    engine_.SetControl(static_cast<Control>(event.index), event.value);
                }
                break;
            case EngineEvent::kModel: engine_.SetModel(event.index); break;
            case EngineEvent::kPitchBend: engine_.SetPitchBend(event.data); break;
            case EngineEvent::kOutputFilter: break; // the adapter's own
        }
    }

    void Render(float* left, float* right) override { engine_.Render(left, right); }

    // Tune and its switch at once, without a glide: for a start from a known state.
    void JumpTune(double tune, bool free) {
        tune_ = static_cast<float>(Clamp01(tune));
        tune_step_ = TuneStep(tune);
        tune_free_ = free;
        engine_.JumpControl(Control::Tune, SoundingTune());
    }

    // How a Tune event carries its semitone step in EngineEvent::data.
    static constexpr int kTuneStepOffset = static_cast<int>(kTuneRange);

  private:
    float SoundingTune() const {
        return static_cast<float>(tune_free_ ? Clamp01(tune_) : TuneNormalized(tune_step_));
    }

    DryEngine& engine_;
    float tune_ = static_cast<float>(kDefaultTune); // for free tuning
    int tune_step_ = TuneStep(kDefaultTune);       // for tuning in semitones
    bool tune_free_ = false;
};

// Tune, from the host's normalized value: the value itself for free tuning,
// and the semitone step, rounded in double precision as the display does.
inline EngineEvent TuneEvent(double normalized) {
    EngineEvent event;
    event.type = EngineEvent::kControl;
    event.index = static_cast<std::uint8_t>(Control::Tune);
    event.value = static_cast<float>(Clamp01(normalized));
    event.data = static_cast<std::uint16_t>(TuneStep(normalized) + EngineBridge::kTuneStepOffset);
    return event;
}

static_assert(StreamAdapter::kQuantum == DryEngine::kQuantum);
static_assert(kTuneFreeControl < StreamAdapter::kControlKinds);

inline EngineEvent NoteEvent(unsigned note, unsigned velocity) {
    EngineEvent event;
    event.type = EngineEvent::kNoteOn;
    event.index = static_cast<std::uint8_t>(note);
    event.data = static_cast<std::uint16_t>(velocity);
    return event;
}

// Any control but Tune, which needs TuneEvent.
inline EngineEvent ControlEvent(Control control, float value) {
    EngineEvent event;
    event.type = EngineEvent::kControl;
    event.index = static_cast<std::uint8_t>(control);
    event.value = value;
    return event;
}

inline EngineEvent TuneFreeEvent(bool free) {
    EngineEvent event;
    event.type = EngineEvent::kControl;
    event.index = static_cast<std::uint8_t>(kTuneFreeControl);
    event.value = free ? 1.0f : 0.0f;
    return event;
}

inline EngineEvent ModelEvent(unsigned model) {
    EngineEvent event;
    event.type = EngineEvent::kModel;
    event.index = static_cast<std::uint8_t>(model);
    return event;
}

inline EngineEvent BendEvent(unsigned raw14) {
    EngineEvent event;
    event.type = EngineEvent::kPitchBend;
    event.data = static_cast<std::uint16_t>(raw14);
    return event;
}

inline EngineEvent FilterEvent(float value) {
    EngineEvent event;
    event.type = EngineEvent::kOutputFilter;
    event.value = value;
    return event;
}

} // namespace torus_classic
