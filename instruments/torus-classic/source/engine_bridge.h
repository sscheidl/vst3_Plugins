// Connects the dry engine (layer 2) to the streaming adapter (layer 3): the
// adapter's events become engine calls. Free of VST3 types, so the tests can
// use the same connection the plug-in uses.
#pragma once

#include "dry_engine.h"
#include "stream_adapter.h"

namespace torus_classic {

class EngineBridge final : public QuantumEngine {
  public:
    explicit EngineBridge(DryEngine& engine) : engine_(engine) {}

    void HandleEvent(const EngineEvent& event) override {
        switch(event.type) {
            case EngineEvent::kNoteOn: engine_.NoteOn(event.index, event.data); break;
            case EngineEvent::kControl:
                if(event.index < kControlCount)
                    engine_.SetControl(static_cast<Control>(event.index), event.value);
                break;
            case EngineEvent::kModel: engine_.SetModel(event.index); break;
            case EngineEvent::kPitchBend: engine_.SetPitchBend(event.data); break;
        }
    }

    void Render(float* left, float* right) override { engine_.Render(left, right); }

  private:
    DryEngine& engine_;
};

static_assert(StreamAdapter::kQuantum == DryEngine::kQuantum);

inline EngineEvent NoteEvent(unsigned note, unsigned velocity) {
    EngineEvent event;
    event.type = EngineEvent::kNoteOn;
    event.index = static_cast<std::uint8_t>(note);
    event.data = static_cast<std::uint16_t>(velocity);
    return event;
}

inline EngineEvent ControlEvent(Control control, float value) {
    EngineEvent event;
    event.type = EngineEvent::kControl;
    event.index = static_cast<std::uint8_t>(control);
    event.value = value;
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

} // namespace torus_classic
