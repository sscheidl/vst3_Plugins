// Connects the instrument engine (layer 2) to the streaming adapter (layer
// 3): the adapter's events become engine calls. Free of VST3 types, so the
// tests can use the same connection the plug-in uses.
#pragma once

#include "engine.h"
#include "stream_adapter.h"

namespace elements_synth {

class EngineBridge final : public QuantumEngine {
  public:
    explicit EngineBridge(Engine& engine) : engine_(engine) {}

    void HandleEvent(const EngineEvent& event) override {
        switch(event.type) {
            case EngineEvent::kNoteOn: engine_.NoteOn(event.index, event.data); break;
            case EngineEvent::kNoteOff: engine_.NoteOff(event.index); break;
            case EngineEvent::kAllNotesOff: engine_.AllNotesOff(); break;
            case EngineEvent::kControl:
                if(event.index < kControlCount)
                    engine_.SetControl(static_cast<Control>(event.index), event.value);
                break;
            case EngineEvent::kPitchBend: engine_.SetPitchBend(event.value); break;
        }
    }

    void Render(float* left, float* right) override { engine_.Render(left, right); }

  private:
    Engine& engine_;
};

static_assert(kControlCount <= StreamAdapter::kControlKinds);
static_assert(StreamAdapter::kQuantum <= Engine::kMaxQuantum);

inline EngineEvent NoteOnEvent(unsigned note, unsigned velocity) {
    EngineEvent event;
    event.type = EngineEvent::kNoteOn;
    event.index = static_cast<std::uint8_t>(note);
    event.data = static_cast<std::uint16_t>(velocity);
    return event;
}

inline EngineEvent NoteOffEvent(unsigned note) {
    EngineEvent event;
    event.type = EngineEvent::kNoteOff;
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

inline EngineEvent BendEvent(float semitones) {
    EngineEvent event;
    event.type = EngineEvent::kPitchBend;
    event.value = semitones;
    return event;
}

} // namespace elements_synth
