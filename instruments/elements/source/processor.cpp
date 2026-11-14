#include "processor.h"

#include "plugin_ids.h"
#include "state_io.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace elements_synth {

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
    // The screen keyboard's notes, for the host to record or pass on.
    addEventOutput(STR16("MIDI Out"), 1);
    // The engine runs at 48 kHz whatever the host's rate is. Usable before
    // the host has named one.
    if(!engine_.Prepare(StreamAdapter::kInternalRate))
        return kResultFalse;
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
    engine_.Reset(Engine::kDefaultSeed);
    for(unsigned i = 0; i < kControlCount; ++i)
        engine_.JumpControl(static_cast<Control>(i), static_cast<float>(values[i]));
    engine_.JumpPitchBend(
        static_cast<float>(PitchBendSemitones(values[static_cast<std::size_t>(Index(kParamPitchBend))])));
}

void Processor::SendStatus() {
    IPtr<IMessage> message = owned(allocateMessage());
    if(!message || !message->getAttributes())
        return; // a host without messages: the window shows no rates
    message->setMessageID(kStatusMessageId);
    message->getAttributes()->setFloat(kStatusHostRate, adapter_.HostRate());
    message->getAttributes()->setFloat(kStatusEngineRate, engine_.SampleRate());
    message->getAttributes()->setInt(kStatusLatency, adapter_.LatencySamples());
    sendMessage(message);
}

tresult PLUGIN_API Processor::setActive(TBool state) {
    // Not called while process() runs, and on the UI thread, which also puts
    // the screen keyboard's notes into the queue. Screen keys pressed while
    // the audio was off are not played later; releases are not needed after
    // the restart below.
    KeyboardNote stale;
    while(keyboard_notes_.Pop(stale))
        keyboard_taken_ = stale.sequence;
    releases_overflowed_.store(false, std::memory_order_relaxed);
    for(auto& slot : overflowed_releases_)
        slot.store(0, std::memory_order_relaxed);
    late_releases_.fill(0);
    late_release_count_ = 0;
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
        SendStatus();
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
    if(index < static_cast<int>(kControlCount))
        adapter_.AddEvent(offset, ControlEvent(static_cast<Control>(index), static_cast<float>(value)));
    else if(id == kParamPitchBend)
        adapter_.AddEvent(offset, BendEvent(static_cast<float>(PitchBendSemitones(value))));
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
        // steps as the hardware smooths its pots. Offsets outside the block
        // are pulled into it.
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
    // The host's list can be full: then the event did not go out.
    return output->addEvent(event) == kResultOk;
}

void Processor::ReleaseToHost(IEventList* output, int note) {
    const auto key = static_cast<std::size_t>(note);
    if(!host_keys_.test(key))
        return; // its note-on never went out
    if(SendToHost(output, note, 0)) {
        host_keys_.reset(key);
        owed_releases_.reset(key);
    } else {
        owed_releases_.set(key);
    }
}

void Processor::StrikeToHost(IEventList* output, int note, int velocity) {
    const auto key = static_cast<std::size_t>(note);
    if(host_keys_.test(key)) // pressed again without a release in between
        ReleaseToHost(output, note);
    if(host_keys_.test(key))
        return; // the host still holds the key: no second note-on before its note-off
    if(SendToHost(output, note, velocity))
        host_keys_.set(key);
}

void Processor::ReleaseKey(IEventList* output, int note) {
    // The engine ignores the release of a key it does not hold.
    adapter_.AddEvent(0, NoteOffEvent(static_cast<unsigned>(note)));
    ReleaseToHost(output, note);
}

void Processor::TakeLateReleases(IEventList* output) {
    if(late_release_count_ == 0)
        return;
    for(int note = 0; note < 128; ++note) {
        const std::uint64_t after = late_releases_[static_cast<std::size_t>(note)];
        if(after == 0 || after - 1 > keyboard_taken_)
            continue;
        late_releases_[static_cast<std::size_t>(note)] = 0;
        --late_release_count_;
        ReleaseKey(output, note);
    }
}

void Processor::ReadKeyboard(IEventList* output) {
    // After an activation: keys the host still believes held are released.
    if(release_held_keys_) {
        release_held_keys_ = false;
        owed_releases_ |= host_keys_;
    }
    // Note-offs the host's list did not take before go out first.
    for(int note = 0; note < 128 && owed_releases_.any(); ++note)
        if(owed_releases_.test(static_cast<std::size_t>(note)))
            ReleaseToHost(output, note);
    // Releases that found the queue full.
    if(releases_overflowed_.exchange(false, std::memory_order_acquire)) {
        for(std::size_t note = 0; note < 128; ++note) {
            const std::uint64_t after =
                overflowed_releases_[note].exchange(0, std::memory_order_relaxed);
            if(after == 0)
                continue;
            if(late_releases_[note] == 0)
                ++late_release_count_;
            late_releases_[note] = std::max(late_releases_[note], after);
        }
    }
    TakeLateReleases(output);
    // Screen keyboard notes start with the block: they come from the UI and
    // have no sample position. At most one queue's worth per call, so the work
    // stays bounded even if the UI keeps adding notes meanwhile. Each key
    // goes to the engine and to the host's MIDI output, down and up.
    KeyboardNote key;
    Clock::time_point now{};
    bool have_time = false;
    for(std::size_t i = 0; i < kKeyboardQueueSize && keyboard_notes_.Pop(key); ++i) {
        keyboard_taken_ = key.sequence;
        PlayKeyboardNote(output, key, now, have_time);
        TakeLateReleases(output);
    }
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
    adapter_.AddEvent(0, NoteOnEvent(key.note, key.velocity));
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
        // Validate before any rounding or cast.
        const bool on = event.type == Event::kNoteOnEvent;
        const int32 pitch = on ? event.noteOn.pitch : event.noteOff.pitch;
        if(pitch < 0 || pitch > 127)
            continue;
        // Offsets outside the block or running backwards are pulled into order.
        const int32 offset = std::max(std::clamp<int32>(event.sampleOffset, 0, last), previous);
        previous = offset;
        const float velocity = on ? event.noteOn.velocity : 0.0f;
        if(!on || !(velocity > 0.0f) || !std::isfinite(velocity)) {
            // A note-off, or a note-on with velocity 0, which MIDI uses for
            // one. A velocity that is no number releases too: the safe side.
            adapter_.AddEvent(offset, NoteOffEvent(static_cast<unsigned>(pitch)));
            continue;
        }
        const long scaled = std::lround(std::min(velocity, 1.0f) * 127.0f);
        adapter_.AddEvent(offset, NoteOnEvent(static_cast<unsigned>(pitch),
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
        // The engine's output stays within +-1, but the sinc converter can
        // overshoot that by a few decibels when it is fully driven (EL-R03).
        // The last stage: clip there. At 48 kHz nothing is converted and this
        // changes nothing.
        for(int32 i = 0; i < frames; ++i) {
            chunk_left_[i] = std::clamp(chunk_left_[i], -1.0f, 1.0f);
            chunk_right_[i] = std::clamp(chunk_right_[i], -1.0f, 1.0f);
        }
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
    // of notes says nothing: the resonator and the reverb ring on.
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
    const std::uint64_t sequence = keyboard_pushed_ + 1;
    if(keyboard_notes_.Push({static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(velocity),
                             Clock::now(), sequence})) {
        keyboard_pushed_ = sequence;
        return kResultOk;
    }
    // A full queue drops a strike and says so: nothing sounds that should not.
    if(velocity > 0)
        return kResultFalse;
    // A release is kept aside, to be played after everything queued before
    // it. Every later note waits for room in the queue and so comes after it.
    overflowed_releases_[static_cast<std::size_t>(note)].store(keyboard_pushed_ + 1,
                                                               std::memory_order_relaxed);
    releases_overflowed_.store(true, std::memory_order_release);
    return kResultOk;
}

FUnknown* CreateProcessorInstance(void*) {
    return Processor::createInstance(nullptr);
}

} // namespace elements_synth
