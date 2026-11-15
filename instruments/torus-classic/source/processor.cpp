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

// The engine control a parameter drives directly, with the value the engine
// gets for it. False for Tune (which goes through the bridge, with its
// switch), Model, Pitch Bend, Filter and the switch itself.
bool DirectControl(int index, double value, Control& control, float& engine_value) {
    engine_value = static_cast<float>(value);
    switch(kFirstParam + static_cast<std::uint32_t>(index)) {
        case kParamStructure: control = Control::Structure; return true;
        case kParamBrightness: control = Control::Brightness; return true;
        case kParamDamping:
            // A higher Damping damps more; the engine has it the other way round.
            control = Control::Damping;
            engine_value = static_cast<float>(EngineDamping(value));
            return true;
        case kParamPosition: control = Control::Position; return true;
        case kParamExcitation: control = Control::Excitation; return true;
        case kParamVolume: control = Control::Volume; return true;
        default: return false;
    }
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
    // The screen keyboard's notes, for the host to record or pass on.
    addEventOutput(STR16("MIDI Out"), 1);
    // Usable before the host has named a sample rate.
    adapter_.Prepare(StreamAdapter::kInternalRate);
    Restart(state_.Current());
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

void Processor::Restart(const ParameterState::Values& values) {
    // Everything comes from the one set of values handed in: a state that is
    // loaded meanwhile cannot mix into it.
    adapter_.Reset();
    engine_.Reset(DryEngine::kDefaultSeed);
    for(int i = 0; i < kParamCount; ++i) {
        Control control;
        float engine_value;
        if(DirectControl(i, values[i], control, engine_value))
            engine_.JumpControl(control, engine_value);
    }
    bridge_.JumpTune(values[Index(kParamTune)], IsOn(values[Index(kParamTuneFree)]));
    engine_.JumpModel(static_cast<unsigned>(ModelIndex(values[Index(kParamModel)])));
    engine_.JumpPitchBend(static_cast<unsigned>(PitchBendRaw(values[Index(kParamPitchBend)])));
    adapter_.JumpOutputFilter(static_cast<float>(values[Index(kParamFilter)]));
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
        // A state loaded since the last block is taken over here instead of in
        // process(). Either way the restart begins from the current state:
        // the last loaded one plus the automation that came after it.
        ParameterState::Loaded pending;
        while(state_.TakeLoaded(pending)) {
            state_.Adopt(pending.generation);
            keyboard_valid_after_ = pending.loaded_at;
        }
        Restart(state_.Current());
        // The queue just emptied may have held the release of a screen key
        // whose strike the host already has: release it at the next block.
        release_held_keys_ = true;
    }
    return AudioEffect::setActive(state);
}

void Processor::ApplyLoadedState(const ParameterState::Loaded& loaded) {
    // The engine starts from this snapshot alone, pitch wheel centred. From
    // here on automation belongs to this state; what an earlier block wrote
    // belongs to the one before and no longer counts.
    state_.Adopt(loaded.generation);
    keyboard_valid_after_ = loaded.loaded_at;
    Restart(loaded.values);
}

void Processor::QueueParameter(int32 offset, ParamID id, double value) {
    if(id < kFirstParam || id >= kFirstParam + kParamCount)
        return;
    const int index = static_cast<int>(id - kFirstParam);
    value = Clamp01(value);
    state_.Write(index, value);
    Control control;
    float engine_value;
    if(DirectControl(index, value, control, engine_value)) {
        adapter_.AddEvent(offset, ControlEvent(control, engine_value));
        return;
    }
    switch(id) {
        case kParamTune:
            // As set, with its semitone; the bridge picks one by the switch.
            adapter_.AddEvent(offset, TuneEvent(value));
            break;
        case kParamTuneFree: adapter_.AddEvent(offset, TuneFreeEvent(IsOn(value))); break;
        case kParamModel:
            adapter_.AddEvent(offset, ModelEvent(static_cast<unsigned>(ModelIndex(value))));
            break;
        case kParamPitchBend:
            adapter_.AddEvent(offset, BendEvent(static_cast<unsigned>(PitchBendRaw(value))));
            break;
        case kParamFilter:
            adapter_.AddEvent(offset, FilterEvent(static_cast<float>(value)));
            break;
        default: break;
    }
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

bool Processor::SendKey(IEventList* output, int note, int velocity) {
    const auto key = static_cast<std::size_t>(note);
    Event event{};
    event.busIndex = 0;
    event.sampleOffset = 0;
    event.flags = Event::kIsLive;
    if(velocity > 0) {
        event.type = Event::kNoteOnEvent;
        event.noteOn.pitch = static_cast<int16>(note);
        event.noteOn.velocity = static_cast<float>(velocity) / 127.0f;
        event.noteOn.noteId = -1;
    } else {
        event.type = Event::kNoteOffEvent;
        event.noteOff.pitch = static_cast<int16>(note);
        event.noteOff.noteId = -1;
    }
    // What the host holds changes only with an event it has taken. A
    // note-off that finds no room, or no list, is due again next block.
    if(output && output->addEvent(event) == kResultOk) {
        held_keys_.set(key, velocity > 0);
        release_due_.reset(key);
        return true;
    }
    if(velocity == 0)
        release_due_.set(key);
    return false;
}

bool Processor::Released(std::size_t key, std::uint32_t strike) const {
    // Counts that wrap around still compare right.
    const std::uint32_t released = key_released_[key].load(std::memory_order_acquire);
    return static_cast<std::int32_t>(released - strike) >= 0;
}

void Processor::ReadKeyboard(IEventList* output) {
    // After an activation: keys the host still believes held are released.
    if(release_held_keys_) {
        release_held_keys_ = false;
        release_due_ |= held_keys_;
    }
    // First the note-offs: keys the host holds whose screen key has been let
    // go, and those due from earlier blocks. They need no room in the queue,
    // so none is lost; one the host's list has no room for comes next block.
    if(held_keys_.any())
        for(std::size_t key = 0; key < held_keys_.size(); ++key)
            if(held_keys_.test(key) && (release_due_.test(key) || Released(key, sent_strike_[key])))
                SendKey(output, static_cast<int>(key), 0);
    // Then the strikes, which start with the block: they come from the UI and
    // have no sample position. At most one queue's worth per call, so the work
    // stays bounded even if the UI keeps adding notes meanwhile. Each goes to
    // the engine and, as a note-on, to the host's MIDI output.
    KeyboardNote strike;
    Clock::time_point now{};
    bool have_time = false;
    for(std::size_t i = 0; i < kKeyboardQueueSize && keyboard_notes_.Pop(strike); ++i) {
        if(!have_time) {
            now = Clock::now();
            have_time = true;
        }
        if(now - strike.struck > kKeyboardNoteLifetime)
            continue; // struck while nobody was rendering
        if(strike.struck <= keyboard_valid_after_)
            continue; // struck before the state that was loaded since
        adapter_.AddEvent(0, NoteEvent(strike.note, strike.velocity));
        // Struck again without a release in between: the host's note ends
        // first. If that note-off finds no room, the host keeps the old note
        // until it does, and gets no second note-on for the same key.
        if(held_keys_.test(strike.note) && !SendKey(output, strike.note, 0))
            continue;
        if(!SendKey(output, strike.note, strike.velocity))
            continue; // the host has no note to end
        sent_strike_[strike.note] = strike.number;
        if(Released(strike.note, strike.number)) // let go already: a short tap
            SendKey(output, strike.note, 0);
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
    ParameterState::Loaded loaded;
    if(state_.TakeLoaded(loaded))
        ApplyLoadedState(loaded);

    const int32 num_samples = std::max<int32>(data.numSamples, 0);
    if(data.inputParameterChanges)
        ReadParameterChanges(*data.inputParameterChanges, num_samples);
    // Keyboard notes wait for a block with audio: taken in a parameter flush
    // they would sit in the adapter, past their lifetime, until audio resumes.
    if(num_samples > 0)
        ReadKeyboard(data.outputEvents);
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
    // One complete state: the last loaded one with the automation since.
    const ParameterState::Values current = state_.Current();
    SavedValues values;
    for(std::size_t i = 0; i < values.size(); ++i)
        values[i] = current[static_cast<std::size_t>(kSavedIndex[i])];
    return WriteSavedValues(state, values) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API Processor::setState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    // Read and check everything first; a damaged state changes nothing.
    SavedValues saved;
    if(!ReadSavedValues(state, saved))
        return kResultFalse;
    // The values count for getState() at once. The running sound is not part
    // of a patch: the audio thread restarts the engine from this state at its
    // next block; if the plug-in is inactive, setActive() does. The pitch
    // wheel is not part of a patch either and returns to its centre.
    state_.Load(saved);
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
    if(note < 0 || note > 127 || velocity < 0 || velocity > 127)
        return kInvalidArgument; // velocity 0 is the release of the key
    const auto key = static_cast<std::size_t>(note);
    if(velocity == 0) {
        // The release of a key is never refused: it only records that every
        // strike of this key so far has been let go.
        key_released_[key].store(key_struck_[key], std::memory_order_release);
        return kResultOk;
    }
    // A full queue drops the strike and says so.
    const std::uint32_t number = key_struck_[key] + 1;
    if(!keyboard_notes_.Push({static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(velocity),
                              number, Clock::now()}))
        return kResultFalse;
    key_struck_[key] = number;
    return kResultOk;
}

FUnknown* CreateProcessorInstance(void*) {
    return Processor::createInstance(nullptr);
}

} // namespace torus_classic
