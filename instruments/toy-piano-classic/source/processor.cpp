#include "processor.h"

#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>

namespace taureon::toy_piano_classic {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

uint64 ChannelMask(int32 numChannels) {
    return numChannels >= 64 ? ~uint64{0} : (uint64{1} << numChannels) - 1;
}

} // namespace

Processor::Processor() {
    setControllerClass(kControllerUID);
}

tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    const tresult result = AudioEffect::initialize(context);
    if(result != kResultOk)
        return result;
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
    addEventInput(STR16("MIDI In"), 16);
    engine_.Init(toy_piano::kSampleRate);
    return kResultOk;
}

tresult PLUGIN_API Processor::setBusArrangements(SpeakerArrangement* /*inputs*/, int32 numIns,
                                                 SpeakerArrangement* outputs, int32 numOuts) {
    // The engine is mono; both channels of the one stereo bus carry it.
    if(numIns == 0 && numOuts == 1 && outputs && outputs[0] == SpeakerArr::kStereo)
        return kResultTrue;
    return kResultFalse;
}

tresult PLUGIN_API Processor::canProcessSampleSize(int32 symbolicSampleSize) {
    return symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64 ? kResultTrue
                                                                              : kResultFalse;
}

tresult PLUGIN_API Processor::setActive(TBool state) {
    if(state) {
        // The host has fixed the sample rate by now. All notes end here.
        UpdateEngine();
        const double rate = processSetup.sampleRate;
        engine_.Init(rate > 0.0 ? static_cast<float>(rate) : toy_piano::kSampleRate);
    }
    return AudioEffect::setActive(state);
}

void Processor::ReadParameterChanges(IParameterChanges& changes) {
    const int32 count = changes.getParameterCount();
    for(int32 i = 0; i < count; ++i) {
        IParamValueQueue* queue = changes.getParameterData(i);
        if(!queue || queue->getPointCount() <= 0)
            continue;
        // The last point of the block wins. Attack and decay only matter at the
        // next strike, and the master pitch glides anyway.
        int32 offset = 0;
        ParamValue value = 0.0;
        if(queue->getPoint(queue->getPointCount() - 1, offset, value) != kResultOk)
            continue;
        switch(queue->getParameterId()) {
            case kParamMasterPitch: master_pitch_.store(Clamp01(value)); break;
            case kParamAttack: attack_.store(Clamp01(value)); break;
            case kParamDecay: decay_.store(Clamp01(value)); break;
            case kParamVolume: volume_.store(Clamp01(value)); break;
            case kParamPitchBend: pitch_bend_.store(Clamp01(value)); break;
            default: break;
        }
    }
}

void Processor::UpdateEngine() {
    engine_.SetPitch(static_cast<float>(MasterPitchSemitones(master_pitch_.load())
                                        + PitchBendSemitones(pitch_bend_.load())));
    engine_.SetVolume(VolumeGain(volume_.load()));
}

template <typename SampleType>
void Processor::Render(ProcessData& data, SampleType** channels, int32 numChannels) {
    const float attack = AttackSeconds(attack_.load());
    const float decay = DecaySeconds(decay_.load());
    int32 position = 0;
    bool audible = false;
    const auto render_until = [&](int32 end) {
        for(; position < end; ++position) {
            const float sample = engine_.Process();
            audible = audible || sample != 0.0f;
            for(int32 channel = 0; channel < numChannels; ++channel)
                channels[channel][position] = static_cast<SampleType>(sample);
        }
    };

    IEventList* events = data.inputEvents;
    const int32 event_count = events ? events->getEventCount() : 0;
    for(int32 i = 0; i < event_count; ++i) {
        Event event{};
        if(events->getEvent(i, event) != kResultOk || event.type != Event::kNoteOnEvent)
            continue; // Note-off is ignored: every strike is a one-shot, as in the firmware.
        // Notes start at their exact sample, whatever the host's block size.
        render_until(std::clamp(event.sampleOffset, position, data.numSamples));
        const int32 note = event.noteOn.pitch;
        const long velocity = std::lround(event.noteOn.velocity * 127.0f);
        if(note < 0 || note > 127 || !(event.noteOn.velocity > 0.0f))
            continue;
        engine_.NoteOn(static_cast<std::uint8_t>(note),
                       static_cast<std::uint8_t>(std::clamp(velocity, 1L, 127L)), attack, decay);
    }
    render_until(data.numSamples);
    data.outputs[0].silenceFlags = audible ? 0 : ChannelMask(numChannels);
}

tresult PLUGIN_API Processor::process(ProcessData& data) {
    if(data.inputParameterChanges)
        ReadParameterChanges(*data.inputParameterChanges);
    UpdateEngine();

    // Hosts also call process() without audio, only to deliver parameter changes.
    if(data.numSamples <= 0 || data.numOutputs < 1 || !data.outputs)
        return kResultOk;
    AudioBusBuffers& output = data.outputs[0];
    if(output.numChannels <= 0)
        return kResultOk;

    const bool is_double = data.symbolicSampleSize == kSample64;
    if(is_double ? !output.channelBuffers64 : !output.channelBuffers32)
        return kResultOk;
    for(int32 channel = 0; channel < output.numChannels; ++channel)
        if(is_double ? !output.channelBuffers64[channel] : !output.channelBuffers32[channel])
            return kResultOk;

    if(is_double)
        Render(data, output.channelBuffers64, output.numChannels);
    else
        Render(data, output.channelBuffers32, output.numChannels);
    return kResultOk;
}

tresult PLUGIN_API Processor::getState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    IBStreamer stream(state, kLittleEndian);
    if(!stream.writeInt32(kStateVersion) || !stream.writeDouble(master_pitch_.load())
       || !stream.writeDouble(attack_.load()) || !stream.writeDouble(decay_.load())
       || !stream.writeDouble(volume_.load()))
        return kResultFalse;
    return kResultOk;
}

tresult PLUGIN_API Processor::setState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    IBStreamer stream(state, kLittleEndian);
    int32 version = 0;
    double master_pitch = 0.0, attack = 0.0, decay = 0.0, volume = 0.0;
    if(!stream.readInt32(version) || version != kStateVersion || !stream.readDouble(master_pitch)
       || !stream.readDouble(attack) || !stream.readDouble(decay) || !stream.readDouble(volume))
        return kResultFalse;
    master_pitch_.store(Clamp01(master_pitch));
    attack_.store(Clamp01(attack));
    decay_.store(Clamp01(decay));
    volume_.store(Clamp01(volume));
    return kResultOk;
}

FUnknown* CreateProcessorInstance(void*) {
    return Processor::createInstance(nullptr);
}

} // namespace taureon::toy_piano_classic
