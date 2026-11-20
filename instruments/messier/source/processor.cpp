#include "processor.h"

#include "message_text.h"
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

// A number for this processor instance (MS-R17): different for every instance of
// this process, and, by the time and the address mixed in, most likely different
// from those of another process. Never 0.
std::uint64_t MakeInstanceId(const void* address) noexcept {
    static std::atomic<std::uint64_t> made{0};
    std::uint64_t x = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())
                      ^ (made.fetch_add(1, std::memory_order_relaxed) << 40)
                      ^ static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(address));
    // splitmix64's finaliser
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x ? x : 1;
}

// MS-R33: two processors of one process show the same value of parameter 199 only if
// the older one's count exceeds the newer one's by 4099 * m, modulo 65535, where m says
// how many processors later the newer one was made: for the next processor's first
// load, the one before would need 4100 loads. A random start keeps processes apart.
std::uint64_t MakeSampleOffset() noexcept {
    static const std::uint64_t start = MakeInstanceId(&start);
    static std::atomic<std::uint64_t> made{0};
    return (start + 4099u * made.fetch_add(1, std::memory_order_relaxed)) % kSampleChangesMask;
}

} // namespace

Processor::Processor() : instance_id_(MakeInstanceId(this)), sample_offset_(MakeSampleOffset()) {
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
    delete held_project_file_;
}

tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    const tresult result = AudioEffect::initialize(context);
    if(result != kResultOk)
        return result;
    // MS-R19: the buses are allocated; without memory the call fails, the buses made
    // so far are let go, and nothing leaves it as an exception.
    try {
        // The sidechain of 0.2.0, inactive by default, read by nothing in 0.1.0.
        addAudioInput(STR16("Sidechain"), SpeakerArr::kStereo, kAux, 0);
        addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
        addEventInput(STR16("MIDI In"), 16);
        // The screen keyboard's notes, for the host to record or pass on.
        addEventOutput(STR16("MIDI Out"), 1);
    } catch(...) {
        removeAllBusses();
        return kOutOfMemory;
    }
    if(!PrepareAdapter(StreamAdapter::kInternalRate)) {
        removeAllBusses();
        return kOutOfMemory;
    }
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
    const ProcessSetup before = processSetup;
    const tresult result = AudioEffect::setupProcessing(setup);
    if(result != kResultOk)
        return result;
    // The rate is valid, so a failure is memory (MS-R19). The adapter is then as it
    // was, and so is the setup: nothing half changed.
    if(!PrepareAdapter(setup.sampleRate)) {
        processSetup = before;
        return kOutOfMemory;
    }
    latency_.store(static_cast<uint32>(adapter_.LatencySamples()));
    SetFadeRate(setup.sampleRate);
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

bool Processor::CanTakeFile() const {
    // A swap or restart lets go of at most two files; keep room for them.
    return backlog_count_ + 2 <= backlog_.size() / 2;
}

FileData* Processor::TakeIncoming() {
    return CanTakeFile() ? exchange_.Take() : nullptr;
}

bool Processor::RetireLater(const FileData* file) {
    // False if there is no room: then the caller keeps the file and tries again.
    if(!file)
        return true;
    if(backlog_count_ >= backlog_.size())
        return false;
    backlog_[backlog_count_++] = file;
    return true;
}

bool Processor::TakeHandovers(bool audio_thread) {
    // MS-R07: a project's values and its file come apart (ParameterState and
    // project_files_) and go into the engine only together: the file of project n with
    // the values of project n. A file without its values (the values are about to be
    // published) or values without their file (no room to take one) wait; meanwhile
    // the previous sound plays on, complete, and no file from the window is taken.
    ParameterState::Loaded loaded;
    if(state_.TakeLoaded(loaded)) {
        pending_project_ = loaded; // a newer project replaces one still waiting
        has_pending_project_ = true;
    }
    if(CanTakeFile())
        if(FileData* file = project_files_.Take()) {
            // Room for the one it replaces: CanTakeFile() held.
            if(audio_thread)
                RetireLater(held_project_file_);
            else
                delete held_project_file_;
            held_project_file_ = file;
        }
    if(has_pending_project_ && held_project_file_
       && held_project_file_->project < pending_project_.project) {
        // The file of an older project whose values never arrived: superseded.
        if(!audio_thread) {
            delete held_project_file_;
            held_project_file_ = nullptr;
        } else if(RetireLater(held_project_file_)) {
            held_project_file_ = nullptr;
        }
    }
    if(has_pending_project_ && held_project_file_
       && held_project_file_->project == pending_project_.project) {
        const FileData* file = held_project_file_;
        held_project_file_ = nullptr;
        has_pending_project_ = false;
        ApplyLoadedState(pending_project_, file);
        return true;
    }
    return false;
}

FileData* Processor::TakeWindowFile() {
    // Only between projects; and a file older than the one playing (finished before a
    // project that has come in since) is not swapped in.
    if(has_pending_project_ || held_project_file_)
        return nullptr;
    FileData* file = TakeIncoming();
    if(!file)
        return nullptr;
    const FileData* current = engine_.NextFile();
    if(current && file->sequence < current->sequence && RetireLater(file))
        return nullptr; // room was there: CanTakeFile() held
    return file;
}

bool Processor::PrepareAdapter(double rate) noexcept {
    // MS-R22: a successful Prepare starts the adapter's time and queues anew. A RESET
    // press it already holds goes on the new start, once; a failed Prepare leaves the
    // old queues, the press included.
    const bool reset_waiting = adapter_.ControlWaiting(static_cast<int>(kResetControl));
    if(!adapter_.Prepare(rate))
        return false;
    if(reset_waiting)
        adapter_.AddEvent(0, ResetEvent());
    return true;
}

void Processor::SetFadeRate(double rate) {
    gate_.SetFadeStep(rate > 0.0 ? static_cast<float>(1.0 / (0.005 * rate)) : 1.0f / (0.005f * 48000.0f));
}

void Processor::Restart(const ParameterState::Values& values, const FileData* incoming) {
    // Bypass and Stop as the values say, without a fade at a new start.
    gate_.Reset(IsOn(values[static_cast<std::size_t>(Index(kParamBypass))]),
                IsOn(values[static_cast<std::size_t>(Index(kParamStop))]));
    // The engine starts without notes; nobody holds one now.
    screen_held_.reset();
    for(auto& held : host_held_)
        held.reset();
    // MS-R18: a RESET press the adapter has put on a quantum not yet rendered would
    // be dropped by its Reset(); it acts in the first quantum of the new start.
    const bool reset_waiting = adapter_.ControlWaiting(static_cast<int>(kResetControl));
    adapter_.Reset();
    engine_.Reset(incoming ? incoming : engine_.NextFile(), SettingsFrom(values));
    reset_pressed_ = false;
    if(reset_waiting)
        adapter_.AddEvent(0, ResetEvent());
    RetireReleased();
}

tresult PLUGIN_API Processor::setActive(TBool state) {
    KeyboardNote stale;
    while(keyboard_notes_.Pop(stale)) {}
    if(state) {
        const double rate = processSetup.sampleRate;
        if(rate > 0.0 && rate != adapter_.HostRate()) {
            // MS-R19: without memory for the new rate the processor does not become
            // active; the adapter stays as it was.
            RateRatio ratio;
            if(MakeRateRatio(rate, ratio) && !PrepareAdapter(rate))
                return kOutOfMemory;
            latency_.store(static_cast<uint32>(adapter_.LatencySamples()));
        }
        // Not the audio thread: files waiting for the retire queue can be freed here,
        // so that a loaded project and its file are always taken together.
        for(std::size_t i = 0; i < backlog_count_; ++i)
            delete backlog_[i];
        backlog_count_ = 0;
        exchange_.Collect();
        // RESET presses are kept (MS-R13): within their 0.5 s they act in the first
        // block after the activation.
        // A complete project, if one is there, restarts the engine (MS-R07); else the
        // current values with the newest file from the window, without a fade. While a
        // project waits for its other half, the values in the engine stay those of the
        // last project adopted.
        if(!TakeHandovers(false)) {
            FileData* file = TakeWindowFile();
            Restart(has_pending_project_ || held_project_file_ ? adopted_values_ : state_.Current(),
                    file);
            RetireReleased();
        }
        release_held_keys_ = true;
    }
    return AudioEffect::setActive(state);
}

void Processor::ApplyLoadedState(const ParameterState::Loaded& loaded, const FileData* incoming) {
    adopted_values_ = loaded.values;
    state_.Adopt(loaded.generation);
    keyboard_valid_after_ = loaded.loaded_at;
    Restart(loaded.values, incoming);
}

void Processor::QueueParameter(int32 offset, ParamID id, double value) {
    const int index = Index(id);
    if(index < 0 || id == kParamSampleChanges) // that one is the processor's own output
        return;
    value = Clamp01(value);
    state_.Write(index, value);
    Control control;
    if(ControlOf(id, control)) {
        adapter_.AddEvent(offset, ControlEvent(control, PotOf(id, value)));
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
        default: break; // Bypass and Stop: the gate's points (ReadSwitches)
    }
}

void Processor::ReadSwitches(IParameterChanges* changes, int32 num_samples) {
    // MS-R35: Bypass and Stop first, each point at its own sample, so that the plan
    // knows before anything else of the block is read where the engine rests.
    const int32 last = std::max<int32>(num_samples - 1, 0);
    const int32 count = changes ? changes->getParameterCount() : 0;
    for(int32 i = 0; i < count; ++i) {
        IParamValueQueue* queue = changes->getParameterData(i);
        if(!queue)
            continue;
        const ParamID id = queue->getParameterId();
        if(id != kParamBypass && id != kParamStop)
            continue;
        const int32 points = queue->getPointCount();
        for(int32 point = 0; point < points; ++point) {
            int32 offset = 0;
            ParamValue value = 0.0;
            if(queue->getPoint(point, offset, value) != kResultOk)
                continue;
            value = Clamp01(value);
            state_.Write(Index(id), value);
            gate_.AddPoint(id == kParamBypass ? OutputGate::kBypass : OutputGate::kStop,
                           std::clamp<int32>(offset, 0, last), IsOn(value));
        }
    }
    gate_.Plan(num_samples);
}

void Processor::ReadParameterChanges(IParameterChanges& changes, int32 num_samples) {
    const int32 last = std::max<int32>(num_samples - 1, 0);
    const int32 count = changes.getParameterCount();
    for(int32 i = 0; i < count; ++i) {
        IParamValueQueue* queue = changes.getParameterData(i);
        if(!queue)
            continue;
        const ParamID id = queue->getParameterId();
        if(id == kParamBypass || id == kParamStop)
            continue; // read by ReadSwitches
        const int32 points = queue->getPointCount();
        // Every point takes effect at its own time, as a step on the quantum grid;
        // the core smooths the pots. The time is the engine's (MS-R35): a point while
        // the engine rests acts when it goes on.
        for(int32 point = 0; point < points; ++point) {
            int32 offset = 0;
            ParamValue value = 0.0;
            if(queue->getPoint(point, offset, value) != kResultOk)
                continue;
            QueueParameter(gate_.EngineOffset(std::clamp<int32>(offset, 0, last)), id, value);
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

void Processor::EngineNoteOff(int32 offset, int note) {
    // MS-R34: the screen keyboard and the host's MIDI on the keyboard's channel share
    // the engine's note. It ends when the last of the two lets go.
    const auto key = static_cast<std::size_t>(note);
    if(screen_held_.test(key) || host_held_[kKeyboardChannel].test(key))
        return;
    adapter_.AddEvent(offset, NoteOffEvent(kKeyboardChannel, static_cast<unsigned>(note)));
}

void Processor::ReleaseKey(IEventList* output, int note) {
    // Unlike Kekule's engine, this one listens to the release: the last held note
    // sounds, and without one the loop plays at its base pitch and full level.
    const auto key = static_cast<std::size_t>(note);
    const bool held = screen_held_.test(key);
    screen_held_.reset(key);
    if(held) // a key dropped while resting, or struck before a restart, holds nothing
        EngineNoteOff(0, note);
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
    if(gate_.RestingAt(0))
        return; // MS-R36: paused or bypassed, as a host note: none waits for the end
    adapter_.AddEvent(0, NoteOnEvent(kKeyboardChannel, key.note, key.velocity));
    screen_held_.set(static_cast<std::size_t>(key.note));
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
        const int32 host_offset = std::max(std::clamp<int32>(event.sampleOffset, 0, last), previous);
        previous = host_offset;
        // MS-R35: on the engine's time; an event while the engine rests acts when it
        // goes on.
        const int32 offset = gate_.EngineOffset(host_offset);
        const float velocity = on ? event.noteOn.velocity : 0.0f;
        const bool strike = on && velocity > 0.0f && std::isfinite(velocity);
        const auto key = static_cast<std::size_t>(pitch);
        if(!strike) {
            std::bitset<128>& held = host_held_[midi_channel];
            if(!held.test(key))
                continue; // the host holds no such note: nothing to end
            held.reset(key);
            if(midi_channel == kKeyboardChannel)
                EngineNoteOff(offset, pitch); // MS-R34: not while the screen holds it
            else
                adapter_.AddEvent(offset, NoteOffEvent(midi_channel, static_cast<unsigned>(pitch)));
            continue;
        }
        if(gate_.RestingAt(host_offset))
            continue; // paused or bypassed: no new note waits for the end of the pause
        const long scaled = std::lround(std::min(velocity, 1.0f) * 127.0f);
        adapter_.AddEvent(offset,
                          NoteOnEvent(midi_channel, static_cast<unsigned>(pitch),
                                      static_cast<unsigned>(std::clamp(scaled, 1L, 127L))));
        host_held_[midi_channel].set(key);
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
    // Bypass and Stop (0.1.9): faded out, the engine rests; it goes on where it stopped.
    // In between, the gain glides; at full gain nothing is touched, so the sound stays
    // bit for bit the engine's. The segments are the gate's plan of this block, cut at
    // every switch point and where the gain reaches 0 (MS-R35).
    for(int s = 0; s < gate_.SegmentCount(); ++s) {
        const OutputGate::Segment& segment = gate_.SegmentAt(s);
        float gain = segment.gain;
        const bool untouched = segment.gain == 1.0f && segment.target == 1.0f;
        for(int32 done = segment.start; done < segment.end; done += kChunk) {
            const int32 frames = std::min<int32>(kChunk, segment.end - done);
            if(!segment.rendered) {
                std::fill(chunk_left_.begin(), chunk_left_.begin() + frames, 0.0f);
                std::fill(chunk_right_.begin(), chunk_right_.begin() + frames, 0.0f);
            } else {
                adapter_.Process(chunk_left_.data(), chunk_right_.data(), frames);
                if(!untouched)
                    for(int32 i = 0; i < frames; ++i) {
                        gain = OutputGate::Step(gain, segment.target, gate_.FadeStep());
                        chunk_left_[i] *= gain;
                        chunk_right_[i] *= gain;
                    }
            }
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
    }
    if(output && channels > 0)
        output->silenceFlags = audible ? 0 : ChannelMask(channels);
}

void Processor::ReportSampleChanges(IParameterChanges* output) {
    // MS-R09: a file from the window became the sound. The output parameter reaches
    // the controller through the host, on the host's thread, window open or not; the
    // controller then asks for the status and tells the host the project changed.
    const std::uint32_t loads = loader_.UserLoads();
    if(loads == reported_user_loads_ || !output)
        return;
    int32 index = 0;
    IParamValueQueue* queue = output->addParameterData(kParamSampleChanges, index);
    int32 point = 0;
    if(queue && queue->addPoint(0, SampleChangesValue(SampleChangesStep(sample_offset_, loads)), point) == kResultOk)
        reported_user_loads_ = loads; // else again in the next block
}

tresult PLUGIN_API Processor::process(ProcessData& data) {
    // A loaded project first, then its file: the restart takes both at once,
    // without a fade (MS-R02). A file on its own is swapped in with the
    // firmware's fade.
    // Files released in the last block that found no room then go first, so that
    // a project waiting for room can come in now.
    RetireReleased();
    if(!TakeHandovers(true))
        if(FileData* file = TakeWindowFile())
            engine_.SwapFile(file);
    // F2 (0.2.9): while the output rests, a swap would wait for the sound to go on and
    // then let the old file be heard fading out; it finishes now, unheard.
    if(gate_.Resting() && engine_.Swapping())
        engine_.FinishSwapSilently();
    const int32 num_samples = std::max<int32>(data.numSamples, 0);
    // The window's RESET presses (MS-R08). Presses that reach one block act once:
    // the same restart from START. A call without samples leaves them waiting
    // (MS-R18): it renders nothing, and an activation after it would drop them.
    if(num_samples > 0 && reset_presses_.exchange(0, std::memory_order_acq_rel) > 0) {
        const auto pressed = Clock::time_point(Clock::duration(last_reset_press_.load()));
        if(Clock::now() - pressed <= kKeyboardNoteLifetime)
            adapter_.AddEvent(0, ResetEvent());
    }
    ReportSampleChanges(data.outputParameterChanges);

    ReadSwitches(data.inputParameterChanges, num_samples); // plans the block (MS-R35)
    if(data.inputParameterChanges)
        ReadParameterChanges(*data.inputParameterChanges, num_samples);
    if(num_samples > 0)
        ReadKeyboard(data.outputEvents);
    if(data.inputEvents)
        ReadEvents(*data.inputEvents, num_samples);
    if(num_samples > 0)
        Render(data);
    RetireReleased();
    // The window's display: only when asked, a copy of a few hundred bytes. Reads the
    // engine and changes nothing in it.
    if(display_wanted_.load(std::memory_order_relaxed)
       && display_wanted_.exchange(false, std::memory_order_acq_rel)) {
        EngineDisplay display;
        engine_.Display(display);
        display_.Publish(display);
    }
    return kResultOk;
}

tresult PLUGIN_API Processor::getState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    try {
        return SaveState(state);
    } catch(...) {
        return kOutOfMemory; // MS-R01: nothing leaves this call as an exception
    }
}

tresult Processor::SaveState(IBStream* state) {
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
    try {
        return LoadState(state);
    } catch(...) {
        return kOutOfMemory; // MS-R01: reading the state itself could not allocate
    }
}

tresult Processor::LoadState(IBStream* state) {
    // Read and check everything first; a damaged state changes nothing.
    SavedState saved;
    if(!ReadSavedState(state, saved))
        return kResultFalse;
    // The sample now, before the values: the audio thread restarts with both
    // (MS-R02). Both carry the same number of this project load.
    const std::uint64_t project = ++project_serial_;
    // MS-R30: a load request of the window made before this moment is overtaken.
    project_loaded_at_.store(static_cast<int64>(Clock::now().time_since_epoch().count()), std::memory_order_release);
    std::unique_ptr<FileData> file = loader_.LoadProject(saved.path, project);
    if(!file)
        return kOutOfMemory; // not even an empty file: the old project stays
    project_files_.Offer(std::move(file));
    if(project_load_hook_)
        project_load_hook_();
    state_.Load(saved.values, project);
    return kResultOk;
}

void Processor::SendFileStatus(int64 generation) noexcept {
    try {
        SendFileStatusUnchecked(generation);
    } catch(...) {
        // No memory for the message: the window asks again on its next poll.
    }
}

void Processor::SendFileStatusUnchecked(int64 generation) {
    IPtr<IMessage> message = owned(allocateMessage());
    if(!message || !message->getAttributes())
        return;
    const FileStatus status = loader_.Status();
    message->setMessageID(kStatusMessageId);
    IAttributeList* attributes = message->getAttributes();
    // MS-R58: a text the host did not take means no status; the window asks again.
    if(!SetText(attributes, kAttrPath, status.path) || !SetText(attributes, kAttrInfo, status.info)
       || !SetText(attributes, kAttrRequest, status.request) || !SetText(attributes, kAttrDetail, status.detail))
        return;
    attributes->setInt(kAttrState, status.Code());
    attributes->setInt(kAttrSerial, status.serial);
    attributes->setInt(kAttrUserLoads, status.user_loads);
    attributes->setInt(kAttrProcessor, static_cast<int64>(instance_id_));
    attributes->setInt(kAttrGeneration, generation);
    sendMessage(message);
}

void Processor::SendDisplay(IMessage* request, int64 generation) noexcept {
    try {
        SendDisplayUnchecked(request, generation);
    } catch(...) {
        // No memory for the reply: the window asks again on its next tick.
    }
}

void Processor::SendDisplayUnchecked(IMessage* request, int64 generation) {
    display_wanted_.store(true, std::memory_order_release);
    EngineDisplay newest;
    if(display_.Take(newest))
        last_display_ = newest;
    int64 have_wave = 0;
    if(IAttributeList* asked = request->getAttributes())
        asked->getInt(kAttrHaveWave, have_wave);
    IPtr<IMessage> message = owned(allocateMessage());
    if(!message || !message->getAttributes())
        return;
    message->setMessageID(kDisplayMessageId);
    IAttributeList* attributes = message->getAttributes();
    attributes->setInt(kAttrGeneration, generation);
    attributes->setInt(kAttrReply, static_cast<int64>(++display_replies_));
    attributes->setBinary(kAttrFrame, &last_display_, sizeof(EngineDisplay));
    // The overview of the very file the core reads, if the controller lacks it. One
    // that is no longer kept (or never made) is not sent: the window shows none.
    std::shared_ptr<const WaveOverview> overview;
    if(last_display_.sequence != 0
       && static_cast<std::uint64_t>(have_wave) != last_display_.sequence)
        overview = loader_.Overview(last_display_.sequence);
    if(overview)
        attributes->setBinary(kAttrWave, overview.get(), sizeof(WaveOverview));
    sendMessage(message);
}

tresult PLUGIN_API Processor::disconnect(IConnectionPoint* other) {
    const tresult result = AudioEffect::disconnect(other);
    // MS-R30: nothing made before now is taken any more. MS-R31: the keys the old
    // connection still holds are released here, so that no late release is needed
    // (and none can end a note of a newer connection).
    closed_until_ = static_cast<int64>(Clock::now().time_since_epoch().count());
    for(int note = 0; note < 128 && keyboard_keys_down_count_ > 0; ++note) {
        const auto key = static_cast<std::size_t>(note);
        if(!keyboard_keys_down_.test(key))
            continue;
        // The queue always keeps room for every release (see notify).
        if(keyboard_notes_.Push(KeyboardNote{static_cast<std::uint8_t>(note), 0, Clock::now()})) {
            keyboard_keys_down_.reset(key);
            --keyboard_keys_down_count_;
        }
    }
    return result;
}

tresult PLUGIN_API Processor::notify(IMessage* message) {
    if(!message || !message->getMessageID())
        return AudioEffect::notify(message);
    // MS-R23: a host may make a message's attribute list on first access.
    try {
        return NotifyUnchecked(message);
    } catch(...) {
        return kOutOfMemory;
    }
}

tresult Processor::NotifyUnchecked(IMessage* message) {
    const char* id = message->getMessageID();
    const bool own = std::strcmp(id, kLoadMessageId) == 0 || std::strcmp(id, kStatusRequestId) == 0
                     || std::strcmp(id, kDisplayRequestId) == 0 || std::strcmp(id, kResetMessageId) == 0
                     || std::strcmp(id, kNoteMessageId) == 0;
    if(!own)
        return AudioEffect::notify(message);
    // MS-R21, MS-R28, MS-R30, MS-R31: a request delivered late is refused before it
    // changes anything (no load, no reset, no note, no reply), when
    // * it was made before this processor was last disconnected: the old connection is
    //   closed, whatever arrives from it later (its held keys were released then);
    // * it is a load made before the last project load: the project overtakes it;
    // * its connection generation is older than one already heard (generations grow
    //   across all controllers of the process).
    // A message without these attributes is not one of this controller's and is
    // judged by the generation alone.
    int64 generation = 0, issued = 0;
    bool has_issued = false;
    if(IAttributeList* attributes = message->getAttributes()) {
        if(attributes->getInt(kAttrGeneration, generation) != kResultOk)
            generation = 0;
        has_issued = attributes->getInt(kAttrIssued, issued) == kResultOk;
    }
    if(has_issued && issued <= closed_until_)
        return kResultFalse;
    if(has_issued && std::strcmp(id, kLoadMessageId) == 0
       && issued <= project_loaded_at_.load(std::memory_order_acquire))
        return kResultFalse;
    if(generation < latest_generation_)
        return kResultFalse;
    latest_generation_ = generation;
    if(std::strcmp(id, kLoadMessageId) == 0) {
        std::string path;
        try {
            if(!GetText(message->getAttributes(), kAttrPath, path))
                return kInvalidArgument;
        } catch(...) {
            return kOutOfMemory;
        }
        if(path.empty()
           || path.size() > static_cast<std::size_t>(kMaxPathBytes) || !IsValidUtf8(path))
            return kInvalidArgument;
        loader_.Request(path);
        SendFileStatus(generation);
        return kResultOk;
    }
    if(std::strcmp(id, kStatusRequestId) == 0) {
        SendFileStatus(generation);
        return kResultOk;
    }
    if(std::strcmp(id, kDisplayRequestId) == 0) {
        SendDisplay(message, generation);
        return kResultOk;
    }
    if(std::strcmp(id, kResetMessageId) == 0) {
        last_reset_press_.store(Clock::now().time_since_epoch().count());
        reset_presses_.fetch_add(1, std::memory_order_acq_rel);
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
