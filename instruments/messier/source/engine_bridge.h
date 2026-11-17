// Connects the looper engine (layer 2) to the streaming adapter (layer 3): the
// adapter's events become engine calls. Free of VST3 types, so the tests can use
// the same connection the plug-in uses.
#pragma once

#include "looper_engine.h"
#include "parameters.h"
#include "stream_adapter.h"

namespace messier {

// Control indices among the adapter's control events: the engine's controls, then
// Freeze and Reset.
constexpr unsigned kFreezeControl = kControlCount;
constexpr unsigned kResetControl = kControlCount + 1;
static_assert(kResetControl < static_cast<unsigned>(StreamAdapter::kControlKinds));
static_assert(StreamAdapter::kQuantum == LooperEngine::kQuantum);
static_assert(StreamAdapter::kInternalRate == LooperEngine::kSampleRate);

class EngineBridge final : public QuantumEngine {
  public:
    explicit EngineBridge(LooperEngine& engine) : engine_(engine) {}

    void HandleEvent(const EngineEvent& event) override {
        switch(event.type) {
            case EngineEvent::kNoteOn: engine_.NoteOn(event.channel, event.index, event.data); break;
            case EngineEvent::kNoteOff: engine_.NoteOff(event.channel, event.index); break;
            case EngineEvent::kAllNotesOff: engine_.AllNotesOff(); break;
            case EngineEvent::kPitchBend: engine_.SetPitchBend(event.data); break;
            case EngineEvent::kControl:
                if(event.index < kControlCount)
                    engine_.SetControl(static_cast<Control>(event.index), event.value);
                else if(event.index == kFreezeControl)
                    engine_.SetFreeze(event.value >= 0.5f);
                else if(event.index == kResetControl)
                    engine_.TriggerReset();
                break;
        }
    }

    void Render(const float* in_left, const float* in_right, float* left, float* right) override {
        engine_.Render(in_left, in_right, left, right);
    }

  private:
    LooperEngine& engine_;
};

inline EngineEvent NoteOnEvent(unsigned channel, unsigned note, unsigned velocity) {
    EngineEvent event;
    event.type = EngineEvent::kNoteOn;
    event.channel = static_cast<std::uint8_t>(channel);
    event.index = static_cast<std::uint8_t>(note);
    event.data = static_cast<std::uint16_t>(velocity);
    return event;
}

inline EngineEvent NoteOffEvent(unsigned channel, unsigned note) {
    EngineEvent event;
    event.type = EngineEvent::kNoteOff;
    event.channel = static_cast<std::uint8_t>(channel);
    event.index = static_cast<std::uint8_t>(note);
    return event;
}

inline EngineEvent ControlEvent(Control control, float value) {
    EngineEvent event;
    event.type = EngineEvent::kControl;
    event.index = static_cast<std::uint8_t>(control);
    event.value = value;
    return event;
}

inline EngineEvent FreezeEvent(bool on) {
    EngineEvent event;
    event.type = EngineEvent::kControl;
    event.index = static_cast<std::uint8_t>(kFreezeControl);
    event.value = on ? 1.0f : 0.0f;
    return event;
}

inline EngineEvent ResetEvent() {
    EngineEvent event;
    event.type = EngineEvent::kControl;
    event.index = static_cast<std::uint8_t>(kResetControl);
    event.value = 1.0f;
    return event;
}

inline EngineEvent BendEvent(unsigned raw14) {
    EngineEvent event;
    event.type = EngineEvent::kPitchBend;
    event.data = static_cast<std::uint16_t>(raw14);
    return event;
}

// The engine control a parameter drives, or false for Freeze, Reset and the wheel.
inline bool ControlOf(std::uint32_t id, Control& control) {
    if(IsPrimary(id)) {
        control = static_cast<Control>(id - kFirstPrimary);
        return true;
    }
    if(IsSecondary(id)) {
        control = static_cast<Control>(kPrimaryCount + (id - kFirstSecondary));
        return true;
    }
    if(id == kParamVolume) {
        control = Control::Volume;
        return true;
    }
    return false;
}

// The settings a restart begins with, from the parameters' normalized values.
template <typename Values>
EngineSettings SettingsFrom(const Values& values) {
    EngineSettings settings;
    for(int i = 0; i < kParamCount; ++i) {
        Control control;
        if(ControlOf(kParamIds[static_cast<std::size_t>(i)], control))
            settings.control[static_cast<unsigned>(control)] =
                static_cast<float>(Clamp01(values[static_cast<std::size_t>(i)]));
    }
    settings.freeze = IsOn(values[static_cast<std::size_t>(Index(kParamFreeze))]);
    settings.bend = static_cast<unsigned>(PitchBendRaw(values[static_cast<std::size_t>(Index(kParamPitchBend))]));
    return settings;
}

} // namespace messier
