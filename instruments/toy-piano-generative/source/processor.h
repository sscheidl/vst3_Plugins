#pragma once

#include "engine.h"
#include "generative_sequencer.h"
#include "handover.h"
#include "parameters.h"
#include "spsc_queue.h"
#include "step_clock.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <atomic>
#include <mutex>

namespace taureon::toy_piano_generative {

// Audio side of the plug-in: the sequencer follows the host's transport and
// plays the engine; MIDI notes play it too, or transpose the sequence.
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
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    // Notes from the screen keyboard arrive here, on the host's UI thread.
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

  private:
    void TakeRequestedState();
    void ReadParameterChanges(Steinberg::Vst::IParameterChanges& changes);
    void UpdateEngine();
    void PublishState();
    // One sixteenth: the sequencer's step, and its note if it sounds.
    void Step(float attack, float decay);
    template <typename SampleType>
    void Render(Steinberg::Vst::ProcessData& data, SampleType** channels,
                Steinberg::int32 numChannels);

    Engine engine_;
    // Normalized values. The host may set the state from another thread while
    // audio is running.
    std::atomic<double> master_pitch_{kDefaultMasterPitch};
    std::atomic<double> attack_{kDefaultAttack};
    std::atomic<double> decay_{kDefaultDecay};
    std::atomic<double> volume_{kDefaultVolume};
    std::atomic<double> pitch_bend_{kDefaultPitchBend};
    std::atomic<double> tuning_{kDefaultTuning};
    std::atomic<bool> run_{true};
    std::atomic<bool> keys_transpose_{true};

    // The sequencer belongs to the audio thread alone. Its settings live in
    // it: the eight sequencer parameters are written there and nowhere else.
    generative::Sequencer sequencer_;
    StepClock clock_;
    bool running_ = false;       // the sequencer stepped in the last block
    bool state_changed_ = false; // the sequencer differs from what was published

    // The audio thread publishes the sequencer's state after every change,
    // for getState(). setState() asks for a state to be taken over; the audio
    // thread does that at the start of its next block. Each request has a
    // number, and a published state names the request it stems from, so that
    // getState() can tell whether its answer is already the loaded one.
    SharedWords<kStateWords> published_;
    SharedWords<kStateWords> requested_;
    std::atomic<std::uint32_t> request_number_{0};
    std::uint32_t taken_number_ = 0; // audio thread
    // getState() and setState() among themselves; never taken by the audio thread.
    std::mutex state_mutex_;
    generative::State loaded_; // what the newest request holds; under the mutex

    // Screen keyboard notes, from notify() to process(). The firmware's own
    // lock-free queue, which there carries USB-MIDI notes to the audio callback.
    struct KeyboardNote {
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
    };
    static constexpr std::size_t kKeyboardQueueSize = 64;
    toy_piano::SpscQueue<KeyboardNote, kKeyboardQueueSize> keyboard_notes_;
};

} // namespace taureon::toy_piano_generative
