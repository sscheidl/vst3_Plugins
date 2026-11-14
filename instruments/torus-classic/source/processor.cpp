#include "processor.h"

#include "plugin_ids.h"
#include "state_io.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace torus_classic {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

uint64 ChannelMask(int32 numChannels) {
    return numChannels >= 64 ? ~uint64{0} : (uint64{1} << numChannels) - 1;
}

// The engine control behind a parameter, or false for Model and Pitch Bend.
bool ControlOf(int index, Control& control) {
    switch(kFirstParam + static_cast<std::uint32_t>(index)) {
        case kParamTune: control = Control::Tune; return true;
        case kParamStructure: control = Control::Structure; return true;
        case kParamBrightness: control = Control::Brightness; return true;
        case kParamDamping: control = Control::Damping; return true;
        case kParamPosition: control = Control::Position; return true;
        case kParamExcitation: control = Control::Excitation; return true;
        case kParamVolume: control = Control::Volume; return true;
        default: return false;
    }
}

} // namespace

Processor::Processor() {
    setControllerClass(kControllerUID);
    for(int i = 0; i < kParamCount; ++i)
        values_[i].store(kDefaults[i]);
}

tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    const tresult result = AudioEffect::initialize(context);
    if(result != kResultOk)
        return result;
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
    addEventInput(STR16("MIDI In"), 16);
    // Usable before the host has named a sample rate.
    adapter_.Prepare(StreamAdapter::kInternalRate);
    Restart();
    return kResultOk;
}

tresult PLUGIN_API Processor::setBusArrangements(SpeakerArrangement* /*inputs*/, int32 numIns,
                                                 SpeakerArrangement* outputs, int32 numOuts) {
    // No audio input; one stereo output.
    if(numIns == 0 && numOuts == 1 && outputs && outputs[0] == SpeakerArr::kStereo)
        return kResultTrue;
    return kResultFalse;
}

tresult PLUGIN_API Processor::canProcessSampleSize(int32 symbolicSampleSize) {
    // The engine computes in float; 64-bit buffers get its values converted.
    return symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64 ? kResultTrue
                                                                              : kResultFalse;
}

tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    RateRatio ratio;
    if(!MakeRateRatio(setup.sampleRate, ratio))
        return kResultFalse;
    const tresult result = AudioEffect::setupProcessing(setup);
    if(result != kResultOk)
        return result;
    // The host calls this while the plug-in is inactive: the place to
    // allocate, and to fix the latency before the host asks for it.
    if(!adapter_.Prepare(setup.sampleRate))
        return kResultFalse;
    latency_.store(static_cast<uint32>(adapter_.LatencySamples()));
    return kResultOk;
}

uint32 PLUGIN_API Processor::getLatencySamples() {
    return latency_.load();
}

void Processor::Restart() {
    adapter_.Reset();
    engine_.Reset(DryEngine::kDefaultSeed);
    for(int i = 0; i < kParamCount; ++i) {
        Control control;
        if(ControlOf(i, control))
            engine_.JumpControl(control, static_cast<float>(Value(i)));
    }
    engine_.JumpModel(static_cast<unsigned>(ModelIndex(Value(kParamModel - kFirstParam))));
    engine_.JumpPitchBend(
        static_cast<unsigned>(PitchBendRaw(Value(kParamPitchBend - kFirstParam))));
}

tresult PLUGIN_API Processor::setActive(TBool state) {
    // Not called while process() runs. Keyboard notes struck while the audio
    // was off are not played later.
    KeyboardNote stale;
    while(keyboard_notes_.Pop(stale)) {}
    if(state) {
        const double rate = processSetup.sampleRate;
        if(rate > 0.0 && rate != adapter_.HostRate() && adapter_.Prepare(rate))
            latency_.store(static_cast<uint32>(adapter_.LatencySamples()));
        // A state loaded since the last block is applied here instead of in
        // process(): its values again, in case automation of a block that was
        // running during setState() wrote over them.
        LoadedState pending;
        bool loaded = false;
        while(loaded_.Take(pending))
            loaded = true;
        if(loaded)
            ApplyLoadedState(pending);
        else
            Restart();
    }
    return AudioEffect::setActive(state);
}

void Processor::ApplyLoadedState(const LoadedState& loaded) {
    // Also puts the values back in case automation of an earlier block wrote
    // over them between setState() and now.
    for(int i = 0; i < kSavedParamCount; ++i)
        values_[i].store(loaded.values[i]);
    values_[kParamPitchBend - kFirstParam].store(kDefaultPitchBend);
    keyboard_valid_after_ = loaded.loaded_at;
    Restart();
}

void Processor::QueueParameter(int32 offset, ParamID id, double value) {
    if(id < kFirstParam || id >= kFirstParam + kParamCount)
        return;
    const int index = static_cast<int>(id - kFirstParam);
    value = Clamp01(value);
    values_[index].store(value);
    Control control;
    if(ControlOf(index, control))
        adapter_.AddEvent(offset, ControlEvent(control, static_cast<float>(value)));
    else if(id == kParamModel)
        adapter_.AddEvent(offset, ModelEvent(static_cast<unsigned>(ModelIndex(value))));
    else
        adapter_.AddEvent(offset, BendEvent(static_cast<unsigned>(PitchBendRaw(value))));
}

void Processor::ReadParameterChanges(IParameterChanges& changes, int32 num_samples) {
    const int32 last = std::max<int32>(num_samples - 1, 0);
    const int32 count = changes.getParameterCount();
    for(int32 i = 0; i < count; ++i) {
        IParamValueQueue* queue = changes.getParameterData(i);
        if(!queue)
            continue;
        const ParamID id = queue->getParameterId();
        const int32 points = queue->getPointCount();
        // Every point takes effect at its own time; the engine smooths the
        // steps over 5 ms. Offsets outside the block are pulled into it.
        for(int32 point = 0; point < points; ++point) {
            int32 offset = 0;
            ParamValue value = 0.0;
            if(queue->getPoint(point, offset, value) != kResultOk)
                continue;
            QueueParameter(std::clamp<int32>(offset, 0, last), id, value);
        }
    }
}

void Processor::ReadKeyboard() {
    // Screen keyboard notes start with the block: they come from the UI and
    // have no sample position. At most one queue's worth per call, so the work
    // stays bounded even if the UI keeps adding notes meanwhile.
    KeyboardNote key;
    Clock::time_point now{};
    bool have_time = false;
    for(std::size_t i = 0; i < kKeyboardQueueSize && keyboard_notes_.Pop(key); ++i) {
        if(!have_time) {
            now = Clock::now();
            have_time = true;
        }
        if(now - key.struck > kKeyboardNoteLifetime)
            continue; // struck while nobody was rendering
        if(key.struck <= keyboard_valid_after_)
            continue; // struck before the state that was loaded since
        adapter_.AddEvent(0, NoteEvent(key.note, key.velocity));
    }
}

void Processor::ReadEvents(IEventList& events, int32 num_samples) {
    const int32 last = std::max<int32>(num_samples - 1, 0);
    const int32 count = events.getEventCount();
    int32 previous = 0;
    for(int32 i = 0; i < count; ++i) {
        Event event{};
        if(events.getEvent(i, event) != kResultOk || event.type != Event::kNoteOnEvent)
            continue; // note-off included: a struck resonator rings until it has decayed
        // Validate before any rounding or cast.
        const int32 pitch = event.noteOn.pitch;
        const float velocity = event.noteOn.velocity;
        if(pitch < 0 || pitch > 127 || !(velocity > 0.0f) || !std::isfinite(velocity))
            continue; // velocity 0 is a note-off; NaN is nothing
        const long scaled = std::lround(std::min(velocity, 1.0f) * 127.0f);
        // Offsets outside the block or running backwards are pulled into order.
        const int32 offset = std::max(std::clamp<int32>(event.sampleOffset, 0, last), previous);
        previous = offset;
        adapter_.AddEvent(offset, NoteEvent(static_cast<unsigned>(pitch),
                                            static_cast<unsigned>(std::clamp(scaled, 1L, 127L))));
    }
}

void Processor::Render(ProcessData& data) {
    // Which output buffers can be written, if any.
    AudioBusBuffers* output = data.numOutputs >= 1 && data.outputs ? &data.outputs[0] : nullptr;
    const bool is_double = data.symbolicSampleSize == kSample64;
    int32 channels = output ? output->numChannels : 0;
    if(output && (is_double ? !output->channelBuffers64 : !output->channelBuffers32))
        channels = 0;
    for(int32 channel = 0; channel < channels; ++channel)
        if(is_double ? !output->channelBuffers64[channel] : !output->channelBuffers32[channel])
            channels = 0;

    // Time passes for the engine whether or not there is somewhere to write.
    bool audible = false;
    for(int32 done = 0; done < data.numSamples; done += kChunk) {
        const int32 frames = std::min<int32>(kChunk, data.numSamples - done);
        adapter_.Process(chunk_left_.data(), chunk_right_.data(), frames);
        for(int32 channel = 0; channel < channels; ++channel) {
            // Left, right, and silence on anything beyond stereo.
            const float* source =
                channel == 0 ? chunk_left_.data() : (channel == 1 ? chunk_right_.data() : nullptr);
            if(is_double) {
                Sample64* target = output->channelBuffers64[channel] + done;
                for(int32 i = 0; i < frames; ++i)
                    target[i] = source ? static_cast<Sample64>(source[i]) : 0.0;
            } else {
                Sample32* target = output->channelBuffers32[channel] + done;
                for(int32 i = 0; i < frames; ++i)
                    target[i] = source ? source[i] : 0.0f;
            }
        }
        for(int32 i = 0; i < frames && !audible; ++i)
            audible = chunk_left_[i] != 0.0f || chunk_right_[i] != 0.0f;
    }
    // Silent only if every sample of this block is exactly zero. The absence
    // of new notes says nothing: resonators ring on.
    if(output && channels > 0)
        output->silenceFlags = audible ? 0 : ChannelMask(channels);
}

tresult PLUGIN_API Processor::process(ProcessData& data) {
    LoadedState loaded;
    if(loaded_.Take(loaded))
        ApplyLoadedState(loaded);

    const int32 num_samples = std::max<int32>(data.numSamples, 0);
    if(data.inputParameterChanges)
        ReadParameterChanges(*data.inputParameterChanges, num_samples);
    // Keyboard notes wait for a block with audio: taken in a parameter flush
    // they would sit in the adapter, past their lifetime, until audio resumes.
    if(num_samples > 0)
        ReadKeyboard();
    if(data.inputEvents)
        ReadEvents(*data.inputEvents, num_samples);

    // Hosts also call process() without audio, only to deliver parameter
    // changes. They wait in the adapter for the next quantum.
    if(num_samples > 0)
        Render(data);
    return kResultOk;
}

tresult PLUGIN_API Processor::getState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    SavedValues values;
    for(int i = 0; i < kSavedParamCount; ++i)
        values[i] = Value(i);
    return WriteSavedValues(state, values) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API Processor::setState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    // Read and check everything first; a damaged state changes nothing.
    LoadedState loaded;
    if(!ReadSavedValues(state, loaded.values))
        return kResultFalse;

    const std::lock_guard<std::mutex> lock(load_mutex_);
    for(int i = 0; i < kSavedParamCount; ++i)
        values_[i].store(loaded.values[i]);
    // The pitch wheel is not part of a patch: back to the centre.
    values_[kParamPitchBend - kFirstParam].store(kDefaultPitchBend);
    // The running sound is not part of a patch either. The audio thread
    // restarts the engine with these values at its next block; if the plug-in
    // is inactive, setActive() does.
    loaded.loaded_at = Clock::now();
    loaded_.Publish(loaded);
    return kResultOk;
}

tresult PLUGIN_API Processor::notify(IMessage* message) {
    if(!message || !message->getMessageID()
       || std::strcmp(message->getMessageID(), kNoteMessageId) != 0)
        return AudioEffect::notify(message);
    IAttributeList* attributes = message->getAttributes();
    int64 note = -1, velocity = 0;
    if(!attributes || attributes->getInt(kNoteMessageNote, note) != kResultOk
       || attributes->getInt(kNoteMessageVelocity, velocity) != kResultOk)
        return kInvalidArgument;
    if(note < 0 || note > 127 || velocity < 1 || velocity > 127)
        return kInvalidArgument;
    // A full queue drops the note and says so.
    if(!keyboard_notes_.Push({static_cast<std::uint8_t>(note),
                              static_cast<std::uint8_t>(velocity), Clock::now()}))
        return kResultFalse;
    return kResultOk;
}

FUnknown* CreateProcessorInstance(void*) {
    return Processor::createInstance(nullptr);
}

} // namespace torus_classic
