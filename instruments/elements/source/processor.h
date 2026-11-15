#pragma once

#include "engine.h"
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

namespace elements_synth {

// Audio side of the plug-in (layer 4): MIDI notes and parameter changes in,
// stereo audio out; the screen keyboard's notes also out as MIDI. Owns one
// engine, which always runs at 48 kHz, and one streaming adapter, which
// converts to the host's rate where that differs. After Torus Classic 0.2.0.
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
    // The resonator rings on after the last note, a frozen reverb without end.
    Steinberg::uint32 PLUGIN_API getTailSamples() SMTG_OVERRIDE {
        return Steinberg::Vst::kInfiniteTail;
    }
    // Loading a state is the other documented reset; it takes effect at the
    // start of the next process() call, on the audio thread. Screen keyboard
    // keys pressed before the load are dropped with it.
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    // Notes from the screen keyboard arrive here, on the host's UI thread.
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

    // Room for the screen keyboard's notes between two blocks. Releases always
    // find room (see keyboard_keys_down_); strikes are refused when the queue
    // would no longer have it.
    static constexpr std::size_t kKeyboardQueueSize = 256;
    // A screen keyboard note that no process() call with audio has picked up
    // after this time is discarded: keys clicked while the host is not
    // rendering do not sound later.
    static constexpr std::chrono::milliseconds kKeyboardNoteLifetime{500};

    // For tests.
    const StreamAdapter& Adapter() const { return adapter_; }
    const Engine& SynthEngine() const { return engine_; }
    Engine& SynthEngineForTest() { return engine_; }

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
    void ReleaseKey(Steinberg::Vst::IEventList* output, int note);
    void PlayKeyboardNote(Steinberg::Vst::IEventList* output, const KeyboardNote& key,
                          Clock::time_point& now, bool& have_time);
    // A screen key to the host's MIDI output. A note-off the host's list does
    // not take is owed and sent again in later blocks; until it is through,
    // the key gets no new note-on there.
    bool SendToHost(Steinberg::Vst::IEventList* output, int note, int velocity);
    void StrikeToHost(Steinberg::Vst::IEventList* output, int note, int velocity);
    void ReleaseToHost(Steinberg::Vst::IEventList* output, int note);
    // Tells the controller the rates in use, for its status line.
    void SendStatus();
    void ReadEvents(Steinberg::Vst::IEventList& events, Steinberg::int32 num_samples);
    void Render(Steinberg::Vst::ProcessData& data);

    Engine engine_;
    EngineBridge bridge_{engine_};
    StreamAdapter adapter_{bridge_};
    std::atomic<Steinberg::uint32> latency_{0};

    // The normalized values of the parameters and the hand-over of loaded
    // states, see parameter_state.h.
    ParameterState state_;

    // Strikes and releases of the screen keyboard, in the order they came.
    // Every release goes through this one queue, so nothing can overtake it.
    SpscQueue<KeyboardNote, kKeyboardQueueSize> keyboard_notes_;
    // The keys whose strike went into the queue and whose release has not:
    // the queue always keeps room for one release of each. A strike is only
    // taken if, with it, that room is still there; a release of a key that is
    // not down has nothing to end and is not queued. Owned by the UI thread,
    // the queue's one producer.
    std::bitset<128> keyboard_keys_down_;
    std::size_t keyboard_keys_down_count_ = 0;
    // Keyboard notes struck up to this moment belong to the time before the
    // last loaded state and are not played. Owned by the audio thread.
    Clock::time_point keyboard_valid_after_{};
    // Screen keys the host has a note-on for and no note-off yet, and of
    // those the ones whose note-off is due but did not get through. Owned by
    // the audio thread.
    std::bitset<128> host_keys_;
    std::bitset<128> owed_releases_;
    bool release_held_keys_ = false;

    // process() renders through these in pieces, then converts to the host's
    // sample format.
    static constexpr int kChunk = 256;
    std::array<float, kChunk> chunk_left_{}, chunk_right_{};
};

} // namespace elements_synth
