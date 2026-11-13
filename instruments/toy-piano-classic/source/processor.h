#pragma once

#include "engine.h"
#include "parameters.h"
#include "spsc_queue.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <atomic>

namespace taureon::toy_piano_classic {

// Audio side of the plug-in: MIDI notes in, stereo audio out.
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
    void ReadParameterChanges(Steinberg::Vst::IParameterChanges& changes);
    void UpdateEngine();
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

    // Screen keyboard notes, from notify() to process(). The firmware's own
    // lock-free queue, which there carries USB-MIDI notes to the audio callback.
    struct KeyboardNote {
        std::uint8_t note = 0;
        std::uint8_t velocity = 0;
    };
    toy_piano::SpscQueue<KeyboardNote, 64> keyboard_notes_;
};

} // namespace taureon::toy_piano_classic
