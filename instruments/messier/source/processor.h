#pragma once

#include "engine_bridge.h"
#include "file_loader.h"
#include "looper_engine.h"
#include "parameter_state.h"
#include "parameters.h"
#include "spsc_queue.h"
#include "stream_adapter.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <atomic>
#include <bitset>
#include <chrono>

namespace messier {

// Audio side of the plug-in (layer 4): MIDI notes and parameter changes in, stereo
// audio out; the screen keyboard's notes also out as MIDI. Owns one looper engine,
// which always runs at 48 kHz, one streaming adapter, which converts to the host's
// rate where that differs, and the file loader. After Kekule 0.3.1; the screen
// keyboard's way to the engine, note-offs included, after Mendelejew 0.1.4.
//
// Threads. The engine and the adapter are touched by process() and, while the host
// is not processing, by setupProcessing() and setActive(). Everything from other
// threads is handed over without locks on the audio side: parameter values and
// loaded states through ParameterState, screen keyboard notes through a bounded
// queue, sample files through FileExchange. process() reads no file, allocates
// nothing and frees nothing: files it lets go are retired to the loader's thread.
//
// The audio input is a sidechain bus ("Sidechain", stereo, inactive by default),
// there from 0.1.0 on so that the bus layout of 0.2.0 is the same; 0.1.0 does not
// read it.
class Processor : public Steinberg::Vst::AudioEffect {
  public:
    Processor();
    ~Processor() override;

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
    // Activation is the documented reset: the firmware engine anew, the playhead at
    // START, no waiting notes, time zero. The sample stays.
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    // The delay of the sample rate conversion; 0 at 48 kHz.
    Steinberg::uint32 PLUGIN_API getLatencySamples() SMTG_OVERRIDE;
    // The loop plays on without a held note.
    Steinberg::uint32 PLUGIN_API getTailSamples() SMTG_OVERRIDE {
        return Steinberg::Vst::kInfiniteTail;
    }
    // Loading a state reads its sample at once, in the calling thread, so that an
    // export straight after opening a project does not begin in silence. The restart
    // from the state takes effect at the start of the next process() call, or in
    // setActive(). A missing sample leaves the plug-in silent; the path stays.
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    // Screen keyboard notes, load requests and status requests arrive here, on the
    // host's UI thread; status replies go back from here.
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

    static constexpr std::size_t kKeyboardQueueSize = 256;
    static constexpr std::chrono::milliseconds kKeyboardNoteLifetime{500};
    // Screen keyboard notes go to the engine on this MIDI channel.
    static constexpr unsigned kKeyboardChannel = 0;

    // For tests.
    const StreamAdapter& Adapter() const { return adapter_; }
    const LooperEngine& Engine() const { return engine_; }
    FileLoader& Loader() { return loader_; }

  private:
    using Clock = std::chrono::steady_clock;
    struct KeyboardNote {
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
        Clock::time_point struck{};
    };

    // Engine and adapter from scratch, the parameters at the given values, with
    // `incoming` (a file just taken from the exchange) or else the engine's file.
    void Restart(const ParameterState::Values& values, const FileData* incoming);
    void ApplyLoadedState(const ParameterState::Loaded& loaded, const FileData* incoming);
    // Files the engine let go, to the loader's thread.
    void RetireReleased();
    FileData* TakeIncoming();
    void QueueParameter(Steinberg::int32 offset, Steinberg::Vst::ParamID id, double value);
    void ReadParameterChanges(Steinberg::Vst::IParameterChanges& changes,
                              Steinberg::int32 num_samples);
    void ReadKeyboard(Steinberg::Vst::IEventList* output);
    void ReleaseKey(Steinberg::Vst::IEventList* output, int note);
    void PlayKeyboardNote(Steinberg::Vst::IEventList* output, const KeyboardNote& key,
                          Clock::time_point& now, bool& have_time);
    bool SendToHost(Steinberg::Vst::IEventList* output, int note, int velocity);
    void StrikeToHost(Steinberg::Vst::IEventList* output, int note, int velocity);
    void ReleaseToHost(Steinberg::Vst::IEventList* output, int note);
    void ReadEvents(Steinberg::Vst::IEventList& events, Steinberg::int32 num_samples);
    void Render(Steinberg::Vst::ProcessData& data);
    // Tells the controller what the loader knows. UI thread.
    void SendFileStatus();

    LooperEngine engine_;
    EngineBridge bridge_{engine_};
    StreamAdapter adapter_{bridge_};
    std::atomic<Steinberg::uint32> latency_{0};
    ParameterState state_;
    bool reset_pressed_ = false; // the Reset button as the audio thread last saw it

    FileExchange exchange_;
    FileLoader loader_{exchange_};
    // Released files the retire queue had no room for yet. Owned by the audio
    // side; no new file is taken while it is more than half full.
    std::array<const FileData*, 8> backlog_{};
    std::size_t backlog_count_ = 0;

    SpscQueue<KeyboardNote, kKeyboardQueueSize> keyboard_notes_;
    std::bitset<128> keyboard_keys_down_; // UI thread
    std::size_t keyboard_keys_down_count_ = 0;
    Clock::time_point keyboard_valid_after_{};
    std::bitset<128> host_keys_;
    std::bitset<128> owed_releases_;
    bool release_held_keys_ = false;

    static constexpr int kChunk = 256;
    std::array<float, kChunk> chunk_left_{}, chunk_right_{};
};

} // namespace messier
