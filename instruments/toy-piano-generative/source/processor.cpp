#include "processor.h"

#include "plugin_ids.h"
#include "state_io.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <random>

namespace taureon::toy_piano_generative {

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

uint64 ChannelMask(int32 numChannels) {
    return numChannels >= 64 ? ~uint64{0} : (uint64{1} << numChannels) - 1;
}

// A different start for every instance, so that two of them do not play the
// same pattern. Called when an instance is created, never from the audio thread.
std::uint64_t FreshSeed() {
    static std::atomic<std::uint64_t> instances{0};
    auto seed =
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    try {
        std::random_device device;
        seed ^= static_cast<std::uint64_t>(device()) << 32;
        seed ^= device();
    } catch(...) {
        // No random source: the clock and the count below still differ.
    }
    return seed + 0x9E3779B97F4A7C15ULL * ++instances;
}

// Tells the host that the plug-in itself changed a parameter.
void Report(ProcessData& data, ParamID id, ParamValue value, int32 offset) {
    if(!data.outputParameterChanges)
        return;
    int32 index = 0;
    if(IParamValueQueue* queue = data.outputParameterChanges->addParameterData(id, index))
        queue->addPoint(std::clamp(offset, 0, std::max(data.numSamples - 1, 0)), value, index);
}

bool HasAudio(const ProcessData& data) {
    if(data.numSamples <= 0 || data.numOutputs < 1 || !data.outputs)
        return false;
    const AudioBusBuffers& output = data.outputs[0];
    if(output.numChannels <= 0)
        return false;
    const bool is_double = data.symbolicSampleSize == kSample64;
    if(is_double ? !output.channelBuffers64 : !output.channelBuffers32)
        return false;
    for(int32 channel = 0; channel < output.numChannels; ++channel)
        if(is_double ? !output.channelBuffers64[channel] : !output.channelBuffers32[channel])
            return false;
    return true;
}

Transport TransportOf(const ProcessData& data) {
    Transport transport;
    if(const ProcessContext* context = data.processContext) {
        transport.playing = (context->state & ProcessContext::kPlaying) != 0;
        transport.has_tempo = (context->state & ProcessContext::kTempoValid) != 0;
        transport.tempo = context->tempo;
        transport.has_position = (context->state & ProcessContext::kProjectTimeMusicValid) != 0;
        transport.quarters = context->projectTimeMusic;
    }
    return transport;
}

} // namespace

Processor::Processor() : sequencer_(FreshSeed()) {
    setControllerClass(kControllerUID);
    // The sequencer needs to know whether the host plays, how fast and where.
    processContextRequirements.needTransportState().needTempo().needProjectTimeMusic();
    loaded_ = sequencer_.state();
    published_.Write(PackState(sequencer_.state(), 0));
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
    // Keyboard notes struck while the audio was off are not played later.
    KeyboardNote stale;
    while(keyboard_notes_.Pop(stale)) {}
    if(state) {
        // The host has fixed the sample rate by now. All notes end here, and
        // the sequencer starts anew with the next playing block.
        UpdateEngine();
        const double rate = processSetup.sampleRate;
        engine_.Init(rate > 0.0 ? static_cast<float>(rate) : toy_piano::kSampleRate);
        clock_.Reset();
        running_ = false;
    }
    return AudioEffect::setActive(state);
}

void Processor::TakeRequestedState() {
    if(request_number_.load() == taken_number_)
        return;
    StateWords words{};
    if(!requested_.TryRead(words, 4))
        return; // a new request is being written just now: the next block takes it
    // Checked when it was read from the stream; an invalid one changes nothing.
    sequencer_.Load(UnpackState(words));
    taken_number_ = GenerationOf(words);
    state_changed_ = true;
}

void Processor::ReadParameterChanges(IParameterChanges& changes) {
    const int32 count = changes.getParameterCount();
    for(int32 i = 0; i < count; ++i) {
        IParamValueQueue* queue = changes.getParameterData(i);
        if(!queue || queue->getPointCount() <= 0)
            continue;
        // The last point of the block wins. Attack and decay only matter at the
        // next strike, the master pitch glides anyway, and the sequencer's
        // settings act on its next step.
        int32 offset = 0;
        ParamValue value = 0.0;
        if(queue->getPoint(queue->getPointCount() - 1, offset, value) != kResultOk)
            continue;
        const ParamID id = queue->getParameterId();
        switch(id) {
            case kParamMasterPitch: master_pitch_.store(Clamp01(value)); continue;
            case kParamAttack: attack_.store(Clamp01(value)); continue;
            case kParamDecay: decay_.store(Clamp01(value)); continue;
            case kParamVolume: volume_.store(Clamp01(value)); continue;
            case kParamPitchBend: pitch_bend_.store(Clamp01(value)); continue;
            case kParamTuning: tuning_.store(Clamp01(value)); continue;
            case kParamRun: run_.store(IsOn(value)); continue;
            case kParamKeysTranspose: keys_transpose_.store(IsOn(value)); continue;
            default: break;
        }
        switch(id) {
            case kParamAlgorithm:
                sequencer_.SetAlgorithm(1 + StepIndex(value, kAlgorithmSteps));
                break;
            case kParamMutation:
                sequencer_.SetMutation(generative::MutationPercent(Clamp01(value)));
                break;
            case kParamNoteProbability:
                sequencer_.SetNoteProbability(StepIndex(value, kNoteProbabilitySteps));
                break;
            case kParamScale: {
                // Choosing another scale draws a new pattern; the same one does not.
                const int mode = StepIndex(value, kScaleSteps);
                if(mode != sequencer_.state().scale_mode)
                    sequencer_.ChooseScaleMode(mode);
                break;
            }
            case kParamTonic: sequencer_.SetTonic(StepIndex(value, kTonicSteps)); break;
            case kParamOctave: sequencer_.SetOctave(StepIndex(value, kOctaveSteps)); break;
            case kParamLength: sequencer_.SetLength(1 + StepIndex(value, kLengthSteps)); break;
            case kParamClockDivide:
                sequencer_.SetClockDivide(StepIndex(value, kClockDivideSteps));
                break;
            default: continue;
        }
        state_changed_ = true;
    }
}

void Processor::UpdateEngine() {
    engine_.SetPitch(static_cast<float>(MasterPitchSemitones(master_pitch_.load())
                                        + PitchBendSemitones(pitch_bend_.load())));
    engine_.SetVolume(VolumeGain(volume_.load()));
    engine_.SetTuning(tuning_.load());
}

void Processor::PublishState() {
    if(!state_changed_)
        return;
    published_.Write(PackState(sequencer_.state(), taken_number_));
    state_changed_ = false;
}

void Processor::Step(float attack, float decay) {
    const generative::StepResult step = sequencer_.Tick();
    state_changed_ = true;
    if(step.sounds)
        engine_.NoteOn(SoundingNote(step.note), kSequencerVelocity, attack, decay);
}

template <typename SampleType>
void Processor::Render(ProcessData& data, SampleType** channels, int32 numChannels) {
    const float attack = AttackSeconds(attack_.load());
    const float decay = DecaySeconds(decay_.load());
    const bool transpose = keys_transpose_.load();
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

    // Screen keyboard notes start with the block: they come from the UI and
    // have no sample position. At most one queue's worth per block, so the work
    // here stays bounded even if the UI keeps adding notes meanwhile.
    KeyboardNote key;
    for(std::size_t i = 0; i < kKeyboardQueueSize && keyboard_notes_.Pop(key); ++i)
        engine_.NoteOn(key.note, key.velocity, attack, decay);

    // The sixteenths of this block, each on its exact sample. They are taken
    // from the clock also while the sequencer stands, so that it stays in time.
    int32 tick = 0;
    bool tick_pending = clock_.Next(tick);
    const auto steps_until = [&](int32 end) {
        while(tick_pending && tick <= end) {
            render_until(tick);
            if(running_)
                Step(attack, decay);
            tick_pending = clock_.Next(tick);
        }
    };

    IEventList* events = data.inputEvents;
    const int32 event_count = events ? events->getEventCount() : 0;
    for(int32 i = 0; i < event_count; ++i) {
        Event event{};
        if(events->getEvent(i, event) != kResultOk || event.type != Event::kNoteOnEvent)
            continue; // Note-off is ignored: every strike is a one-shot, as in the firmware.
        // Notes take effect at their exact sample, whatever the host's block size.
        const int32 offset = std::clamp(event.sampleOffset, position, data.numSamples);
        steps_until(offset);
        render_until(offset);
        const int32 note = event.noteOn.pitch;
        const long velocity = std::lround(event.noteOn.velocity * 127.0f);
        if(note < 0 || note > 127 || !(event.noteOn.velocity > 0.0f))
            continue;
        if(transpose) {
            // The key says where the sequence's tonic sounds from now on.
            const KeyTransposition to = TranspositionForKey(note);
            sequencer_.SetTonic(to.tonic);
            sequencer_.SetOctave(to.octave);
            state_changed_ = true;
            Report(data, kParamTonic, StepNormalized(to.tonic, kTonicSteps), offset);
            Report(data, kParamOctave, StepNormalized(to.octave, kOctaveSteps), offset);
            continue;
        }
        engine_.NoteOn(static_cast<std::uint8_t>(note),
                       static_cast<std::uint8_t>(std::clamp(velocity, 1L, 127L)), attack, decay);
    }
    steps_until(data.numSamples);
    render_until(data.numSamples);
    data.outputs[0].silenceFlags = audible ? 0 : ChannelMask(numChannels);
}

tresult PLUGIN_API Processor::process(ProcessData& data) {
    TakeRequestedState();
    if(data.inputParameterChanges)
        ReadParameterChanges(*data.inputParameterChanges);
    UpdateEngine();

    // Hosts also call process() without audio, only to deliver parameter changes.
    if(!HasAudio(data)) {
        PublishState();
        return kResultOk;
    }

    clock_.Begin(TransportOf(data), processSetup.sampleRate, data.numSamples);
    // The sequencer steps while the host plays and Run is on. Whenever that
    // begins, the pattern starts at its first step.
    const bool running = run_.load() && clock_.Playing();
    if(running && !running_) {
        sequencer_.Restart();
        state_changed_ = true;
    }
    running_ = running;

    AudioBusBuffers& output = data.outputs[0];
    if(data.symbolicSampleSize == kSample64)
        Render(data, output.channelBuffers64, output.numChannels);
    else
        Render(data, output.channelBuffers32, output.numChannels);
    PublishState();
    return kResultOk;
}

tresult PLUGIN_API Processor::getState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    const std::lock_guard<std::mutex> lock(state_mutex_);
    SavedState saved;
    saved.master_pitch = master_pitch_.load();
    saved.attack = attack_.load();
    saved.decay = decay_.load();
    saved.volume = volume_.load();
    saved.tuning = tuning_.load();
    saved.run = run_.load();
    saved.keys_transpose = keys_transpose_.load();
    // What the audio thread published, unless a loaded state is still waiting
    // for it: then that one is the plug-in's state.
    const StateWords words = published_.Read();
    saved.sequencer =
        GenerationOf(words) == request_number_.load() ? UnpackState(words) : loaded_;
    IBStreamer stream(state, kLittleEndian);
    return WriteState(stream, saved) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API Processor::setState(IBStream* state) {
    if(!state)
        return kInvalidArgument;
    IBStreamer stream(state, kLittleEndian);
    SavedState saved;
    if(!ReadState(stream, saved))
        return kResultFalse; // nothing was taken over
    const std::lock_guard<std::mutex> lock(state_mutex_);
    master_pitch_.store(saved.master_pitch);
    attack_.store(saved.attack);
    decay_.store(saved.decay);
    volume_.store(saved.volume);
    tuning_.store(saved.tuning);
    run_.store(saved.run);
    keys_transpose_.store(saved.keys_transpose);
    loaded_ = saved.sequencer;
    const std::uint32_t number = request_number_.load() + 1;
    requested_.Write(PackState(loaded_, number));
    request_number_.store(number);
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
    // A full queue drops the note, as the firmware does, and says so.
    if(!keyboard_notes_.Push(
           {static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(velocity)}))
        return kResultFalse;
    return kResultOk;
}

FUnknown* CreateProcessorInstance(void*) {
    return Processor::createInstance(nullptr);
}

} // namespace taureon::toy_piano_generative
