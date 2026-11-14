#pragma once

#include "dry_engine.h"
#include "engine_bridge.h"
#include "parameter_state.h"
#include "parameters.h"
#include "spsc_queue.h"
#include "stream_adapter.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <atomic>
#include <bitset>
#include <chrono>

namespace torus_classic {

// Audio side of the plug-in (layer 4): MIDI notes and parameter changes in,
// dry stereo audio out; the screen keyboard's notes also out as MIDI. Owns one engine and one streaming adapter.
//
// Threads. The engine and the adapter are touched by process() and, while the
// host is not processing, by setupProcessing() and setActive(). Everything
// that comes from other threads is handed over without locks on the audio
// side: parameter values and loaded states through ParameterState, screen
// keyboard notes through a bounded queue.
class Processor : public Steinberg::Vst::AudioEffect {
  public:
    Processor();

    static Steinberg::FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs,
                                                     Steinberg::int32 numIns,
                                                     Steinberg::Vst::SpeakerArrangement* outputs,
                                                     Steinberg::int32 numOuts) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize)
        SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup)
        SMTG_OVERRIDE;
    // Activation is the documented reset: silence, no waiting notes, time zero.
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    // The delay of the sample rate conversion; 0 at 48 kHz. It depends on the
    // sample rate alone, which the host only changes while the plug-in is
    // inactive, and is valid from setupProcessing() on.
    Steinberg::uint32 PLUGIN_API getLatencySamples() SMTG_OVERRIDE;
    // Resonators ring on after the last note, at full Damping without end.
    Steinberg::uint32 PLUGIN_API getTailSamples() SMTG_OVERRIDE {
        return Steinberg::Vst::kInfiniteTail;
    }
    // Loading a state is the other documented reset; it takes effect at the
    // start of the next process() call, on the audio thread. Screen keyboard
    // notes struck before the load are dropped with it.
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    // Notes from the screen keyboard arrive here, on the host's UI thread.
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

    static constexpr std::size_t kKeyboardQueueSize = 64;
    // A screen keyboard note that no process() call with audio has picked up
    // after this time is discarded: keys clicked while the host is not
    // rendering do not sound later.
    static constexpr std::chrono::milliseconds kKeyboardNoteLifetime{500};

    // For tests.
    const StreamAdapter& Adapter() const { return adapter_; }
    const DryEngine& Engine() const { return engine_; }

  private:
    using Clock = std::chrono::steady_clock;
    struct KeyboardNote {
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
        Clock::time_point struck{};
    };

    // Engine and adapter from scratch, the parameters at the given values.
    void Restart(const ParameterState::Values& values);
    void ApplyLoadedState(const ParameterState::Loaded& loaded);
    void QueueParameter(Steinberg::int32 offset, Steinberg::Vst::ParamID id, double value);
    void ReadParameterChanges(Steinberg::Vst::IParameterChanges& changes,
                              Steinberg::int32 num_samples);
    void ReadKeyboard(Steinberg::Vst::IEventList* output);
    // A screen key to the host's MIDI output; velocity 0 is its release.
    void SendKey(Steinberg::Vst::IEventList* output, int note, int velocity);
    void ReadEvents(Steinberg::Vst::IEventList& events, Steinberg::int32 num_samples);
    void Render(Steinberg::Vst::ProcessData& data);

    DryEngine engine_;
    EngineBridge bridge_{engine_};
    StreamAdapter adapter_{bridge_};
    std::atomic<Steinberg::uint32> latency_{0};

    // The normalized values of the parameters 100..108 and the hand-over of
    // loaded states, see parameter_state.h.
    ParameterState state_;

    SpscQueue<KeyboardNote, kKeyboardQueueSize> keyboard_notes_;
    // Keyboard notes struck up to this moment belong to the time before the
    // last loaded state and are not played. Owned by the audio thread.
    Clock::time_point keyboard_valid_after_{};
    // Screen keys the host has a note-on for and no note-off yet. Owned by
    // the audio thread.
    std::bitset<128> held_keys_;
    bool release_held_keys_ = false;

    // process() renders through these in pieces, then converts to the host's
    // sample format.
    static constexpr int kChunk = 256;
    std::array<float, kChunk> chunk_left_{}, chunk_right_{};
};

} // namespace torus_classic
