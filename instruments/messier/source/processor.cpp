#include "processor.h"

#include "plugin_ids.h"
#include "state_io.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace messier {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

uint64 ChannelMask(int32 numChannels) {
    return numChannels >= 64 ? ~uint64{0} : (uint64{1} << numChannels) - 1;
}

// A string attribute as UTF-8 bytes. Binary, so that no host converts it.
bool GetText(IAttributeList* attributes, const char* id, std::string& text) {
    const void* data = nullptr;
    uint32 size = 0;
    if(!attributes || attributes->getBinary(id, data, size) != kResultOk)
        return false;
    text.assign(static_cast<const char*>(data), size);
    return true;
}

void SetText(IAttributeList* attributes, const char* id, const std::string& text) {
    attributes->setBinary(id, text.data(), static_cast<uint32>(text.size()));
}

} // namespace

Processor::Processor() {
    setControllerClass(kControllerUID);
}

Processor::~Processor() {
    // The files the engine still holds and those waiting for the retire queue.
    const FileData* current = engine_.CurrentFile();
    const FileData* next = engine_.NextFile();
    if(next != current)
        delete next;
    delete current;
    while(const FileData* released = engine_.TakeReleased())
        delete released;
    for(std::size_t i = 0; i < backlog_count_; ++i)
        delete backlog_[i];
}

tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    const tresult result = AudioEffect::initialize(context);
    if(result != kResultOk)
        return result;
    // The sidechain of 0.2.0, inactive by default, read by nothing in 0.1.0.
    addAudioInput(STR16("Sidechain"), SpeakerArr::kStereo, kAux, 0);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
    addEventInput(STR16("MIDI In"), 16);
    // The screen keyboard's notes, for the host to record or pass on.
    addEventOutput(STR16("MIDI Out"), 1);
    adapter_.Prepare(StreamAdapter::kInternalRate);
    Restart(state_.Current(), nullptr);
    return kResultOk;
}

tresult PLUGIN_API Processor::setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                                 SpeakerArrangement* outputs, int32 numOuts) {
    // One stereo output; the sidechain stereo, mono or absent.
    if(numOuts != 1 || !outputs || outputs[0] != SpeakerArr::kStereo)
        return kResultFalse;
    if(numIns > 1 || numIns < 0 || (numIns == 1 && !inputs))
        return kResultFalse;
    if(numIns == 1 && inputs[0] != SpeakerArr::kStereo && inputs[0] != SpeakerArr::kMono)
        return kResultFalse;
    if(numIns == 1)
        if(AudioBus* bus = getAudioInput(0))
            bus->setArrangement(inputs[0]);
    return kResultTrue;
}

tresult PLUGIN_API Processor::canProcessSampleSize(int32 symbolicSampleSize) {
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
    if(!adapter_.Prepare(setup.sampleRate))
        return kResultFalse;
    latency_.store(static_cast<uint32>(adapter_.LatencySamples()));
    return kResultOk;
}

uint32 PLUGIN_API Processor::getLatencySamples() {
    return latency_.load();
}

void Processor::RetireReleased() {
    while(backlog_count_ < backlog_.size())
        if(const FileData* file = engine_.TakeReleased())
            backlog_[backlog_count_++] = file;
        else
            break;
    std::size_t kept = 0;
    for(std::size_t i = 0; i < backlog_count_; ++i)
        if(!exchange_.Retire(backlog_[i]))
            backlog_[kept++] = backlog_[i];
    backlog_count_ = kept;
}

FileData* Processor::TakeIncoming() {
    // A swap or restart lets go of at most two files; keep room for them.
    return backlog_count_ + 2 <= backlog_.size() / 2 ? exchange_.Take() : nullptr;
}

void Processor::Restart(const ParameterState::Values& values, const FileData* incoming) {
    adapter_.Reset();
    engine_.Reset(incoming ? incoming : engine_.NextFile(), SettingsFrom(values));
    reset_pressed_ = false;
    RetireReleased();
}

tresult PLUGIN_API Processor::setActive(TBool state) {
    KeyboardNote stale;
    while(keyboard_notes_.Pop(stale)) {}
    if(state) {
        const double rate = processSetup.sampleRate;
        if(rate > 0.0 && rate != adapter_.HostRate() && adapter_.Prepare(rate))
            latency_.store(static_cast<uint32>(adapter_.LatencySamples()));
        ParameterState::Loaded pending;
        while(state_.TakeLoaded(pending)) {
            state_.Adopt(pending.generation);
            keyboard_valid_after_ = pending.loaded_at;
        }
        // The newest file, without a fade: the restart starts with it.
        Restart(state_.Current(), TakeIncoming());
        release_held_keys_ = true;
    }
    return AudioEffect::setActive(state);
}

void Processor::ApplyLoadedState(const ParameterState::Loaded& loaded, const FileData* incoming) {
    state_.Adopt(loaded.generation);
    keyboard_valid_after_ = loaded.loaded_at;
    Restart(loaded.values, incoming);
}

void Processor::QueueParameter(int32 offset, ParamID id, double value) {
    const int index = Index(id);
    if(index < 0)
        return;
    value = Clamp01(value);
    state_.Write(index, value);
    Control control;
    if(ControlOf(id, control)) {
        adapter_.AddEvent(offset, ControlEvent(control, static_cast<float>(value)));
        return;
    }
    switch(id) {
        case kParamFreeze: adapter_.AddEvent(offset, FreezeEvent(IsOn(value))); break;
        case kParamReset: {
            // A button: pressing it acts, letting go does not.
            const bool pressed = IsOn(value);
            if(pressed && !reset_pressed_)
                adapter_.AddEvent(offset, ResetEvent());
            reset_pressed_ = pressed;
            break;
        }
        case kParamPitchBend:
            adapter_.AddEvent(offset, BendEvent(static_cast<unsigned>(PitchBendRaw(value))));
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
        // Every point takes effect at its own time, as a step on the quantum grid;
        // the core smooths the pots.
        for(int32 point = 0; point < points; ++point) {
            int32 offset = 0;
            ParamValue value = 0.0;
            if(queue->getPoint(point, offset, value) != kResultOk)
                continue;
            QueueParameter(std::clamp<int32>(offset, 0, last), id, value);
        }
    }
}

bool Processor::SendToHost(IEventList* output, int note, int velocity) {
    if(!output)
        return false;
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
    return output->addEvent(event) == kResultOk;
}

void Processor::ReleaseToHost(IEventList* output, int note) {
    const auto key = static_cast<std::size_t>(note);
    if(!host_keys_.test(key))
        return;
    if(SendToHost(output, note, 0)) {
        host_keys_.reset(key);
        owed_releases_.reset(key);
    } else {
        owed_releases_.set(key);
    }
}

void Processor::StrikeToHost(IEventList* output, int note, int velocity) {
    const auto key = static_cast<std::size_t>(note);
    if(host_keys_.test(key))
        ReleaseToHost(output, note);
    if(host_keys_.test(key))
        return;
    if(SendToHost(output, note, velocity))
        host_keys_.set(key);
}

void Processor::ReleaseKey(IEventList* output, int note) {
    // Unlike Kekule's engine, this one listens to the release: the last held note
    // sounds, and without one the loop plays at its base pitch and full level.
    adapter_.AddEvent(0, NoteOffEvent(kKeyboardChannel, static_cast<unsigned>(note)));
    ReleaseToHost(output, note);
}

void Processor::ReadKeyboard(IEventList* output) {
    if(release_held_keys_) {
        release_held_keys_ = false;
        owed_releases_ |= host_keys_;
    }
    for(int note = 0; note < 128 && owed_releases_.any(); ++note)
        if(owed_releases_.test(static_cast<std::size_t>(note)))
            ReleaseToHost(output, note);
    KeyboardNote key;
    Clock::time_point now{};
    bool have_time = false;
    for(std::size_t i = 0; i < kKeyboardQueueSize && keyboard_notes_.Pop(key); ++i)
        PlayKeyboardNote(output, key, now, have_time);
}

void Processor::PlayKeyboardNote(IEventList* output, const KeyboardNote& key,
                                 Clock::time_point& now, bool& have_time) {
    if(key.velocity == 0) {
        ReleaseKey(output, key.note);
        return;
    }
    if(!have_time) {
        now = Clock::now();
        have_time = true;
    }
    if(now - key.struck > kKeyboardNoteLifetime)
        return; // struck while nobody was rendering
    if(key.struck <= keyboard_valid_after_)
        return; // struck before the state that was loaded since
    adapter_.AddEvent(0, NoteOnEvent(kKeyboardChannel, key.note, key.velocity));
    StrikeToHost(output, key.note, key.velocity);
}

void Processor::ReadEvents(IEventList& events, int32 num_samples) {
    const int32 last = std::max<int32>(num_samples - 1, 0);
    const int32 count = events.getEventCount();
    int32 previous = 0;
    for(int32 i = 0; i < count; ++i) {
        Event event{};
        if(events.getEvent(i, event) != kResultOk)
            continue;
        if(event.type != Event::kNoteOnEvent && event.type != Event::kNoteOffEvent)
            continue;
        const bool on = event.type == Event::kNoteOnEvent;
        const int32 pitch = on ? event.noteOn.pitch : event.noteOff.pitch;
        const int32 channel = on ? event.noteOn.channel : event.noteOff.channel;
        if(pitch < 0 || pitch > 127)
            continue;
        const auto midi_channel = static_cast<unsigned>(channel >= 0 && channel < 16 ? channel : 0);
        const int32 offset = std::max(std::clamp<int32>(event.sampleOffset, 0, last), previous);
        previous = offset;
        const float velocity = on ? event.noteOn.velocity : 0.0f;
        if(!on || !(velocity > 0.0f) || !std::isfinite(velocity)) {
            adapter_.AddEvent(offset, NoteOffEvent(midi_channel, static_cast<unsigned>(pitch)));
            continue;
        }
        const long scaled = std::lround(std::min(velocity, 1.0f) * 127.0f);
        adapter_.AddEvent(offset,
                          NoteOnEvent(midi_channel, static_cast<unsigned>(pitch),
                                      static_cast<unsigned>(std::clamp(scaled, 1L, 127L))));
    }
}

void Processor::Render(ProcessData& data) {
    AudioBusBuffers* output = nullptr;
    // The output is the only output bus; inputs (the sidechain) are not read.
    if(data.numOutputs >= 1 && data.outputs)
        output = &data.outputs[0];
    const bool is_double = data.symbolicSampleSize == kSample64;
    int32 channels = output ? output->numChannels : 0;
    if(output && (is_double ? !output->channelBuffers64 : !output->channelBuffers32))
        channels = 0;
    for(int32 channel = 0; channel < channels; ++channel)
        if(is_double ? !output->channelBuffers64[channel] : !output->channelBuffers32[channel])
            channels = 0;

    bool audible = false;
    for(int32 done = 0; done < data.numSamples; done += kChunk) {
        const int32 frames = std::min<int32>(kChunk, data.numSamples - done);
        adapter_.Process(chunk_left_.data(), chunk_right_.data(), frames);
        for(int32 channel = 0; channel < channels; ++channel) {
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
    if(output && channels > 0)
        output->silenceFlags = audible ? 0 : ChannelMask(channels);
}

tresult PLUGIN_API Processor::process(ProcessData& data) {
    // A new file first: a state loaded in the same breath restarts with it, without
    // a fade; on its own it is swapped in with the firmware's fade.
    const FileData* incoming = TakeIncoming();
    ParameterState::Loaded loaded;
    if(state_.TakeLoaded(loaded)) {
        ApplyLoadedState(loaded, incoming);
        incoming = nullptr;
    }
    if(incoming)
        engine_.SwapFile(incoming);

    const int32 num_samples = std::max<int32>(data.numSamples, 0);
    if(data.inputParameterChanges)
        ReadParameterChanges(*data.inputParameterChanges, num_samples);
    if(num_samples > 0)
        ReadKeyboard(data.outputEvents);
    if(data.inputEvents)
        ReadEvents(*data.inputEvents, num_samples);
    if(num_samples > 0)
        Render(data);
    RetireReleased();
    return kResultOk;
}

tresult PLUGIN_API Processor::getState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    const ParameterState::Values current = state_.Current();
    SavedState saved;
    for(std::size_t i = 0; i < saved.values.size(); ++i)
        saved.values[i] = current[static_cast<std::size_t>(kSavedIndex[i])];
    saved.path = loader_.SoundPath();
    return WriteSavedState(state, saved) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API Processor::setState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    // Read and check everything first; a damaged state changes nothing.
    SavedState saved;
    if(!ReadSavedState(state, saved))
        return kResultFalse;
    // The sample now, before the values: the audio thread restarts with both.
    loader_.LoadNow(saved.path);
    state_.Load(saved.values);
    return kResultOk;
}

void Processor::SendFileStatus() {
    IPtr<IMessage> message = owned(allocateMessage());
    if(!message || !message->getAttributes())
        return;
    const FileStatus status = loader_.Status();
    message->setMessageID(kStatusMessageId);
    IAttributeList* attributes = message->getAttributes();
    SetText(attributes, kAttrPath, status.path);
    SetText(attributes, kAttrInfo, status.info);
    SetText(attributes, kAttrRequest, status.request);
    SetText(attributes, kAttrDetail, status.detail);
    attributes->setInt(kAttrState, status.Code());
    attributes->setInt(kAttrSerial, status.serial);
    sendMessage(message);
}

tresult PLUGIN_API Processor::notify(IMessage* message) {
    if(!message || !message->getMessageID())
        return AudioEffect::notify(message);
    const char* id = message->getMessageID();
    if(std::strcmp(id, kLoadMessageId) == 0) {
        std::string path;
        if(!GetText(message->getAttributes(), kAttrPath, path) || path.empty()
           || path.size() > static_cast<std::size_t>(kMaxPathBytes) || !IsValidUtf8(path))
            return kInvalidArgument;
        loader_.Request(path);
        SendFileStatus();
        return kResultOk;
    }
    if(std::strcmp(id, kStatusRequestId) == 0) {
        SendFileStatus();
        return kResultOk;
    }
    if(std::strcmp(id, kNoteMessageId) != 0)
        return AudioEffect::notify(message);
    IAttributeList* attributes = message->getAttributes();
    int64 note = -1, velocity = 0;
    if(!attributes || attributes->getInt(kNoteMessageNote, note) != kResultOk
       || attributes->getInt(kNoteMessageVelocity, velocity) != kResultOk)
        return kInvalidArgument;
    if(note < 0 || note > 127 || velocity < 0 || velocity > 127)
        return kInvalidArgument;
    // Called on the UI thread only, the queue's one producer (Mendelejew EL-R01, EL-R06).
    const auto key = static_cast<std::size_t>(note);
    const KeyboardNote item{static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(velocity),
                            Clock::now()};
    if(velocity == 0) {
        if(!keyboard_keys_down_.test(key))
            return kResultOk;
        if(!keyboard_notes_.Push(item))
            return kResultFalse; // cannot happen: the room was kept
        keyboard_keys_down_.reset(key);
        --keyboard_keys_down_count_;
        return kResultOk;
    }
    const std::size_t down = keyboard_keys_down_count_ + (keyboard_keys_down_.test(key) ? 0 : 1);
    if(keyboard_notes_.SizeForProducer() + 1 + down > kKeyboardQueueSize)
        return kResultFalse;
    if(!keyboard_notes_.Push(item))
        return kResultFalse;
    if(!keyboard_keys_down_.test(key)) {
        keyboard_keys_down_.set(key);
        ++keyboard_keys_down_count_;
    }
    return kResultOk;
}

FUnknown* CreateProcessorInstance(void*) {
    return Processor::createInstance(nullptr);
}

} // namespace messier
