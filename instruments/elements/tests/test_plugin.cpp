// Tests of the plug-in as a host sees it (layer 4): buses, notes, velocity,
// pitch wheel, automation, block sizes, sample rates, state, the screen
// keyboard's messages and the status report. The host double is the one of
// Torus Classic 0.2.0. Window tests are left out on purpose: the windowing
// code is Torus Classic's, and looks and handling are checked by hand.
#define TEST_SUPPORT_COUNT_ALLOCATIONS
#include "test_support.h"

#include "controller.h"
#include "editor.h"
#include "parameter_state.h"
#include "parameters.h"
#include "processor.h"
#include "state_io.h"
#include "value_text.h"
#include "view_layout.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace elements_synth;
using namespace test_support;

// In a plug-in the SDK's DLL entry point defines this handle. The test links
// the plug-in's classes into an executable, so it has to provide it itself.
void* moduleHandle = nullptr;

namespace {

// --- A minimal host around one processor (after Torus Classic) ---------------

struct Ev {
    enum Kind { kNoteOn, kNoteOff, kParam };
    std::int64_t sample;
    Kind kind;
    int a;        // pitch or parameter id
    double value; // velocity (0..1) or normalized value
};
using HostScore = std::vector<Ev>;

Ev NoteOn(std::int64_t sample, int pitch, int velocity) {
    return {sample, Ev::kNoteOn, pitch, velocity / 127.0};
}
Ev NoteOff(std::int64_t sample, int pitch) {
    return {sample, Ev::kNoteOff, pitch, 0.0};
}
Ev Param(std::int64_t sample, ParamID id, double value) {
    return {sample, Ev::kParam, static_cast<int>(id), value};
}

struct HostConfig {
    double rate = 48000.0;
    int max_block = 2048;
    bool double_precision = false;
};

class PluginHost {
  public:
    explicit PluginHost(const HostConfig& config, HostScore score = {})
    : config_(config), score_(std::move(score)), events_(4096), out_events_(256), changes_(kParamCount) {
        processor = owned(new Processor());
        CHECK(processor->initialize(nullptr) == kResultOk);
        Setup(config.rate);
        CHECK(processor->setActive(true) == kResultOk);
        processor->setProcessing(true);
    }
    ~PluginHost() {
        processor->setProcessing(false);
        CHECK(processor->setActive(false) == kResultOk);
        CHECK(processor->terminate() == kResultOk);
    }
    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    // setupProcessing as a host does it, while the processor is inactive.
    void Setup(double rate) {
        config_.rate = rate;
        const int32 size = config_.double_precision ? kSample64 : kSample32;
        CHECK(processor->canProcessSampleSize(size) == kResultTrue);
        ProcessSetup setup{kRealtime, size, config_.max_block, rate};
        CHECK(processor->setupProcessing(setup) == kResultOk);
        data_.unprepare();
        CHECK(data_.prepare(*processor.get(), config_.max_block, size));
        CHECK(data_.numOutputs == 1 && data_.outputs[0].numChannels == 2);
        data_.processMode = kRealtime;
        data_.inputEvents = &events_;
        data_.outputEvents = &out_events_;
        data_.inputParameterChanges = &changes_;
    }

    void AddScore(const HostScore& more) { score_.insert(score_.end(), more.begin(), more.end()); }

    // One process() call of `frames` samples, with the score's events of
    // that span. `frames` may be 0: a parameter flush.
    void Process(int frames) {
        CHECK(frames <= config_.max_block);
        events_.clear();
        out_events_.clear();
        changes_.clearQueue();
        const std::int64_t end = position + std::max(frames, 1);
        for(; next_ < score_.size() && score_[next_].sample < end; ++next_) {
            const Ev& ev = score_[next_];
            const auto offset = static_cast<int32>(ev.sample - position);
            if(ev.kind == Ev::kParam) {
                int32 index = 0;
                IParamValueQueue* queue =
                    changes_.addParameterData(static_cast<ParamID>(ev.a), index);
                CHECK(queue && queue->addPoint(offset, ev.value, index) == kResultOk);
            } else {
                Event event{};
                event.sampleOffset = offset;
                if(ev.kind == Ev::kNoteOn) {
                    event.type = Event::kNoteOnEvent;
                    event.noteOn.pitch = static_cast<int16>(ev.a);
                    event.noteOn.velocity = static_cast<float>(ev.value);
                } else {
                    event.type = Event::kNoteOffEvent;
                    event.noteOff.pitch = static_cast<int16>(ev.a);
                    event.noteOff.velocity = static_cast<float>(ev.value);
                }
                CHECK(events_.addEvent(event) == kResultOk);
            }
        }
        data_.numSamples = frames;
        data_.outputs[0].silenceFlags = 0;
        {
            const AllocationProbe probe; // only the plug-in's own work
            CHECK(processor->process(data_) == kResultOk);
            allocations += probe.Count();
        }
        for(int32 i = 0; i < out_events_.getEventCount(); ++i) {
            Event event{};
            CHECK(out_events_.getEvent(i, event) == kResultOk);
            if(event.type == Event::kNoteOnEvent)
                midi_out += "N" + std::to_string(event.noteOn.pitch) + " ";
            else if(event.type == Event::kNoteOffEvent)
                midi_out += "F" + std::to_string(event.noteOff.pitch) + " ";
        }
        bool silent = true;
        for(int i = 0; i < frames; ++i) {
            const double l = config_.double_precision ? data_.outputs[0].channelBuffers64[0][i]
                                                      : data_.outputs[0].channelBuffers32[0][i];
            const double r = config_.double_precision ? data_.outputs[0].channelBuffers64[1][i]
                                                      : data_.outputs[0].channelBuffers32[1][i];
            CHECK(std::isfinite(l) && std::isfinite(r));
            CHECK(std::fabs(l) <= 1.0001 && std::fabs(r) <= 1.0001);
            // 64-bit buffers carry the engine's float values, converted.
            CHECK(static_cast<double>(static_cast<float>(l)) == l);
            left.push_back(static_cast<float>(l));
            right.push_back(static_cast<float>(r));
            silent = silent && l == 0.0 && r == 0.0;
        }
        if(frames > 0)
            CHECK((data_.outputs[0].silenceFlags == 3) == silent); // the flags tell the truth
        position += frames;
    }

    void Run(std::int64_t total, int block = 64) {
        const std::int64_t end = position + total;
        while(position < end)
            Process(static_cast<int>(std::min<std::int64_t>(block, end - position)));
    }
    void RunBlocks(std::int64_t total, const std::vector<int>& blocks) {
        const std::int64_t end = position + total;
        std::size_t which = 0;
        while(position < end)
            Process(static_cast<int>(std::min<std::int64_t>(
                std::min(blocks[which++ % blocks.size()], config_.max_block), end - position)));
    }

    std::vector<float> Stereo() const {
        std::vector<float> both = left;
        both.insert(both.end(), right.begin(), right.end());
        return both;
    }
    void Clear() {
        left.clear();
        right.clear();
    }

    IPtr<Processor> processor;
    std::vector<float> left, right;
    std::string midi_out;
    std::int64_t position = 0;
    std::uint64_t allocations = 0;

  private:
    HostConfig config_;
    HostScore score_;
    std::size_t next_ = 0;
    HostProcessData data_;
    EventList events_, out_events_;
    ParameterChanges changes_;
};

double Cents(double hz, double reference) {
    return 1200.0 * std::log2(hz / reference);
}

double Fundamental(const std::vector<float>& x, double expected, double rate, double from,
                   double to) {
    const auto at = [rate](double seconds) { return static_cast<std::size_t>(seconds * rate); };
    double hz = PeakFrequency(x, expected * 0.97, expected * 1.03, expected * 0.001, rate, at(from),
                              at(to));
    return PeakFrequency(x, hz - expected * 0.001, hz + expected * 0.001, expected * 0.00002, rate,
                         at(from), at(to));
}

double Level(const std::vector<float>& x, double rate, double from, double to) {
    return Decibel(Rms(x, static_cast<std::size_t>(from * rate), static_cast<std::size_t>(to * rate)));
}

tresult SendNote(Processor& processor, int64 pitch, int64 velocity = 100,
                 const char* id = kNoteMessageId) {
    auto message = owned(new HostMessage());
    message->setMessageID(id);
    message->getAttributes()->setInt(kNoteMessageNote, pitch);
    message->getAttributes()->setInt(kNoteMessageVelocity, velocity);
    return processor.notify(message);
}

std::vector<char> SavedState(Processor& processor) {
    MemoryStream stream;
    CHECK(processor.getState(&stream) == kResultOk);
    return std::vector<char>(stream.getData(), stream.getData() + stream.getSize());
}

tresult LoadState(Processor& component, const std::vector<char>& bytes) {
    MemoryStream stream;
    int32 written = 0;
    if(!bytes.empty())
        stream.write(const_cast<char*>(bytes.data()), static_cast<int32>(bytes.size()), &written);
    int64 start = 0;
    stream.seek(0, IBStream::kIBSeekSet, &start);
    return component.setState(&stream);
}

std::vector<char> MakeState(int32 version, const std::vector<double>& values) {
    std::vector<char> bytes(sizeof(version) + values.size() * sizeof(double));
    std::memcpy(bytes.data(), &version, sizeof(version));
    if(!values.empty())
        std::memcpy(bytes.data() + sizeof(version), values.data(), values.size() * sizeof(double));
    return bytes;
}

// A dry, struck patch: the right channel is the resonator.
HostScore DryPatch() {
    return {Param(0, kParamSpace, 0.0), Param(0, kParamDamping, 0.6), Param(0, kParamVelocity, 0.0)};
}

// --- Tests -------------------------------------------------------------------

void TestBuses() {
    IPtr<Processor> processor = owned(new Processor());
    CHECK(processor->initialize(nullptr) == kResultOk);
    // An instrument: no audio input, one stereo output, MIDI in and out.
    CHECK(processor->getBusCount(kAudio, kInput) == 0 && processor->getBusCount(kAudio, kOutput) == 1);
    CHECK(processor->getBusCount(kEvent, kInput) == 1 && processor->getBusCount(kEvent, kOutput) == 1);
    BusInfo info{};
    CHECK(processor->getBusInfo(kAudio, kOutput, 0, info) == kResultOk && info.channelCount == 2);
    SpeakerArrangement stereo = SpeakerArr::kStereo, mono = SpeakerArr::kMono;
    CHECK(processor->setBusArrangements(nullptr, 0, &stereo, 1) == kResultTrue);
    CHECK(processor->setBusArrangements(nullptr, 0, &mono, 1) == kResultFalse);
    CHECK(processor->setBusArrangements(&stereo, 1, &stereo, 1) == kResultFalse);
    CHECK(processor->canProcessSampleSize(kSample32) == kResultTrue);
    CHECK(processor->canProcessSampleSize(kSample64) == kResultTrue);
    CHECK(processor->getTailSamples() == kInfiniteTail);
    // The engine runs at 48 kHz from the start, whatever the host will say.
    CHECK(processor->SynthEngine().SampleRate() == 48000.0);
    CHECK(processor->SynthEngine().Quantum() == StreamAdapter::kQuantum);
    // The rates the validator tries are accepted, nonsense is not.
    for(const double rate : {1234.5678, 22050.0, 44100.0, 48000.0, 96000.0, 192000.0, 1234567.8}) {
        ProcessSetup setup{kRealtime, kSample32, 512, rate};
        CHECK(processor->setupProcessing(setup) == kResultOk);
        CHECK(processor->SynthEngine().SampleRate() == 48000.0);
        CHECK((processor->getLatencySamples() == 0) == (rate == 48000.0));
    }
    for(const double rate : {0.0, -48000.0, std::numeric_limits<double>::quiet_NaN()}) {
        ProcessSetup setup{kRealtime, kSample32, 512, rate};
        CHECK(processor->setupProcessing(setup) != kResultOk);
    }
    CHECK(processor->terminate() == kResultOk);
    std::printf("buses: instrument with one stereo output, MIDI in and out; the engine stays at "
                "48 kHz for every host rate\n");
}

void TestNotes() {
    const double rate = 48000.0;
    HostScore score = DryPatch();
    score.push_back(NoteOn(4800, 69, 100));
    score.push_back(NoteOff(9600, 69));
    PluginHost host({rate}, score);
    host.Run(4800);
    CHECK(Energy(host.Stereo()) == 0.0); // silent until the first note
    host.Process(1);
    CHECK(host.processor->SynthEngine().Gate() && host.processor->SynthEngine().SoundingNote() == 69);
    host.Run(4799);
    host.Run(64);
    CHECK(!host.processor->SynthEngine().Gate()); // the note-off closed the gate
    host.Run(48000);
    CHECK(std::fabs(Cents(Fundamental(host.right, 440.0, rate, 0.15, 1.0), 440.0)) < 1.0);
    CHECK(Level(host.right, rate, 0.1, 0.3) > -50.0);

    // Velocity: with the Velocity parameter up, a hard key is louder; at 0
    // it makes no difference.
    const auto strike = [&](int velocity, double amount) {
        PluginHost one({rate}, {Param(0, kParamSpace, 0.0), Param(0, kParamVelocity, amount),
                                NoteOn(2400, 60, velocity), NoteOff(4800, 60)});
        one.Run(24000);
        return Level(one.right, rate, 0.05, 0.4);
    };
    const double soft = strike(20, 1.0), hard = strike(127, 1.0);
    CHECK(hard > soft + 12.0);
    CHECK(std::fabs(strike(20, 0.0) - strike(127, 0.0)) < 0.01);
    // A note-on with velocity 0 is a note-off; notes outside 0..127 are ignored.
    {
        PluginHost one({rate}, {NoteOn(0, 60, 100), NoteOn(2400, 60, 0), NoteOn(4800, 200, 100)});
        one.Run(2400);
        CHECK(one.processor->SynthEngine().Gate());
        one.Run(4800);
        CHECK(!one.processor->SynthEngine().Gate() && one.processor->SynthEngine().SoundingNote() == 60);
    }

    // The pitch wheel arrives as a parameter.
    {
        HostScore bend = DryPatch();
        bend.push_back(Param(0, kParamDamping, 0.8));
        bend.push_back(NoteOn(0, 69, 100));
        bend.push_back(Param(24000, kParamPitchBend, 1.0));
        bend.push_back(Param(72000, kParamPitchBend, 0.0));
        bend.push_back(Param(120000, kParamPitchBend, 0.5));
        PluginHost one({rate}, bend);
        one.Run(168000);
        const double up = 440.0 * std::pow(2.0, 2.0 / 12.0), down = 440.0 * std::pow(2.0, -2.0 / 12.0);
        CHECK(std::fabs(Cents(Fundamental(one.right, 440.0, rate, 0.05, 0.5), 440.0)) < 1.0);
        CHECK(std::fabs(Cents(Fundamental(one.right, up, rate, 0.6, 1.5), up)) < 2.0);
        CHECK(std::fabs(Cents(Fundamental(one.right, down, rate, 1.6, 2.5), down)) < 2.0);
        CHECK(std::fabs(Cents(Fundamental(one.right, 440.0, rate, 2.6, 3.5), 440.0)) < 1.0);
        CHECK(PitchBendSemitones(kPitchBendCentreRaw / static_cast<double>(kPitchBendMaxRaw)) == 0.0);
        CHECK(PitchBendSemitones(0.0) == -2.0 && PitchBendSemitones(1.0) == 2.0);
    }
    CHECK(host.allocations == 0);
    std::printf("notes: note-on opens the gate, note-off closes it; velocity moves the level by "
                "%.1f dB at full Velocity; the wheel bends by two semitones\n",
                hard - soft);
}

HostScore Performance(double rate, std::int64_t total, std::uint32_t seed) {
    HostScore score;
    Lcg random(seed);
    std::int64_t t = 0;
    while(true) {
        t += 1 + static_cast<std::int64_t>(random.Below(2000) * rate / 48000.0);
        if(t >= total)
            break;
        const std::uint32_t kind = random.Below(10);
        if(kind < 4)
            score.push_back(NoteOn(t, 36 + static_cast<int>(random.Below(48)),
                                   1 + static_cast<int>(random.Below(127))));
        else if(kind < 6)
            score.push_back(NoteOff(t, 36 + static_cast<int>(random.Below(48))));
        else
            score.push_back(Param(t, kFirstParam + random.Below(kParamCount), random.Unit()));
    }
    return score;
}

void TestBlockSizesAndRates() {
    std::uint64_t allocations = 0;
    for(const double rate : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0}) {
        const std::int64_t total = static_cast<std::int64_t>(rate * 2);
        const HostScore score = Performance(rate, total, 77u);
        PluginHost reference({rate}, score);
        reference.Run(total, 64);
        CHECK(Energy(reference.left) > 1e-3 && Energy(reference.right) > 1e-3);
        CHECK(reference.processor->SynthEngine().SampleRate() == 48000.0);
        CHECK(reference.processor->Adapter().Resampling() == (rate != 48000.0));
        CHECK(reference.processor->getLatencySamples()
              == static_cast<uint32>(reference.processor->Adapter().LatencySamples()));
        allocations += reference.allocations;
        for(const std::vector<int>& blocks :
            {std::vector<int>{2048}, std::vector<int>{7, 333, 24, 1, 1024, 13, 480}}) {
            PluginHost other({rate}, score);
            other.RunBlocks(total, blocks);
            CHECK(other.left == reference.left && other.right == reference.right);
            allocations += other.allocations;
        }
        // 64-bit buffers carry the same samples.
        PluginHost wide({rate, 2048, true}, score);
        wide.Run(total, 64);
        CHECK(wide.left == reference.left && wide.right == reference.right);

        // The pitch is right at every host rate.
        HostScore tone = DryPatch();
        tone.push_back(NoteOn(static_cast<std::int64_t>(rate * 0.1), 69, 100));
        PluginHost tuned({rate}, tone);
        tuned.Run(static_cast<std::int64_t>(rate * 1.2));
        CHECK(std::fabs(Cents(Fundamental(tuned.right, 440.0, rate, 0.2, 1.1), 440.0)) < 1.0);
    }
    CHECK(allocations == 0);
    // A parameter flush, process() without samples, is taken and renders nothing.
    PluginHost host({48000.0}, {Param(0, kParamVolume, 0.25)});
    host.Process(0);
    CHECK(host.left.empty());
    std::printf("block sizes and rates: identical output for every block size at 44.1, 48, 88.2, "
                "96 and 192 kHz; A4 within a cent at each; process() allocates nothing\n");
}

void TestAutomation() {
    const double rate = 48000.0;
    // A sustained sound, then the volume to nothing and back.
    HostScore score = {Param(0, kParamStrikeLevel, 0.0), Param(0, kParamBlowLevel, 0.8),
                       Param(0, kParamContour, 0.5), Param(0, kParamSpace, 0.0),
                       NoteOn(0, 57, 100),
                       Param(48000, kParamVolume, 0.0), Param(96000, kParamVolume, 1.0)};
    PluginHost host({rate}, score);
    host.Run(144000);
    CHECK(Level(host.left, rate, 0.5, 1.0) > -50.0);
    CHECK(Energy(host.left, 48000 + 480, 96000) == 0.0); // silent once the ramp is through
    CHECK(Level(host.left, rate, 2.2, 3.0) > Level(host.left, rate, 0.5, 1.0) - 3.0);
    // Parameter IDs the plug-in does not have, and values out of range, do no harm.
    PluginHost odd({rate}, {Param(0, 99, 0.5), Param(0, 120, 0.5), Param(0, kParamSpace, 7.0),
                            Param(0, kParamSpace, -1.0), NoteOn(0, 60, 100)});
    odd.Run(4800);
    CHECK(Energy(odd.Stereo()) > 0.0);
    std::printf("automation: parameter changes act inside the block they arrive in\n");
}

void TestState() {
    const double rate = 48000.0;
    // Every saved parameter away from its default; the wheel too.
    HostScore moved;
    std::vector<double> expected;
    for(int i = 0; i < kSavedParamCount; ++i) {
        double value = 0.07 + 0.045 * i;
        if(kFirstParam + i == kParamCoarse)
            value = CoarseNormalized(7);
        if(kFirstParam + i == kParamFreeze)
            value = 0.0;
        if(kFirstParam + i == kParamStrikeLevel)
            value = 0.9;
        expected.push_back(value);
        moved.push_back(Param(0, kFirstParam + i, value));
    }
    moved.push_back(Param(0, kParamPitchBend, 0.9));
    PluginHost host({rate}, moved);
    host.Run(64);
    const std::vector<char> state = SavedState(*host.processor);
    CHECK(state == MakeState(kStateVersion, expected));

    // Loaded into a fresh instance it is that state again, and sounds the same
    // as an instance that got the values as parameter changes.
    PluginHost loaded({rate});
    CHECK(LoadState(*loaded.processor, state) == kResultOk);
    CHECK(SavedState(*loaded.processor) == state);
    loaded.AddScore({NoteOn(960, 60, 100), NoteOff(24000, 60)});
    loaded.Run(48000);
    HostScore direct = moved;
    direct.back() = Param(0, kParamPitchBend, 0.5); // the wheel is not part of a patch
    PluginHost fresh({rate}, direct);
    // Parameter changes glide, a loaded state is there at once: give the glide time.
    fresh.Run(96000);
    fresh.Clear();
    fresh.AddScore({NoteOn(96000 + 960, 60, 100), NoteOff(96000 + 24000, 60)});
    fresh.Run(48000);
    CHECK(Energy(loaded.right) > 1e-4);
    CHECK(std::fabs(Level(loaded.right, rate, 0.02, 0.5) - Level(fresh.right, rate, 0.02, 0.5)) < 1.0);
    const double f0 = 440.0 * std::pow(2.0, (60 + 7 - 69 + FineSemitones(expected[Index(kParamFine)])) / 12.0);
    CHECK(std::fabs(Cents(Fundamental(loaded.right, f0, rate, 0.05, 0.5), f0)) < 3.0);

    // Loading is a reset: a sounding note ends, the wheel returns to the centre.
    PluginHost ringing({rate}, {Param(0, kParamBlowLevel, 0.8), Param(0, kParamContour, 0.5),
                                NoteOn(0, 57, 100), Param(0, kParamPitchBend, 1.0)});
    ringing.Run(24000);
    CHECK(Energy(ringing.Stereo()) > 1e-3 && ringing.processor->SynthEngine().Gate());
    CHECK(LoadState(*ringing.processor, MakeState(kStateVersion, std::vector<double>(kControlDefaults, kControlDefaults + kSavedParamCount))) == kResultOk);
    ringing.Clear();
    ringing.Run(4800);
    CHECK(Energy(ringing.Stereo()) == 0.0 && !ringing.processor->SynthEngine().Gate());
    CHECK(ringing.processor->SynthEngine().HeldCount() == 0);
    // So is activation.
    ringing.AddScore({NoteOn(ringing.position, 57, 100)});
    ringing.Run(4800);
    CHECK(ringing.processor->SynthEngine().Gate());
    CHECK(ringing.processor->setActive(false) == kResultOk);
    CHECK(ringing.processor->setActive(true) == kResultOk);
    ringing.Clear();
    ringing.Run(4800);
    CHECK(Energy(ringing.Stereo()) == 0.0 && !ringing.processor->SynthEngine().Gate());

    // Damaged states are refused and change nothing.
    const std::vector<char> before = SavedState(*host.processor);
    std::vector<double> bad = expected;
    CHECK(LoadState(*host.processor, {}) != kResultOk);
    CHECK(LoadState(*host.processor, MakeState(kStateVersion + 1, expected)) != kResultOk);
    CHECK(LoadState(*host.processor, MakeState(0, expected)) != kResultOk);
    bad.pop_back();
    CHECK(LoadState(*host.processor, MakeState(kStateVersion, bad)) != kResultOk);
    for(const double poison : {-0.01, 1.01, std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity()}) {
        bad = expected;
        bad[5] = poison;
        CHECK(LoadState(*host.processor, MakeState(kStateVersion, bad)) != kResultOk);
    }
    CHECK(host.processor->setState(nullptr) != kResultOk && host.processor->getState(nullptr) != kResultOk);
    CHECK(SavedState(*host.processor) == before);
    // Stepped values come back on their step.
    bad = expected;
    bad[Index(kParamCoarse)] = 0.51;
    bad[Index(kParamFreeze)] = 0.7;
    CHECK(LoadState(*host.processor, MakeState(kStateVersion, bad)) == kResultOk);
    bad[Index(kParamCoarse)] = CoarseNormalized(CoarseSemitones(0.51));
    bad[Index(kParamFreeze)] = 1.0;
    CHECK(SavedState(*host.processor) == MakeState(kStateVersion, bad));

    // The controller reads the same state; the wheel returns to its centre.
    IPtr<Controller> controller = owned(new Controller());
    CHECK(controller->initialize(nullptr) == kResultOk);
    controller->setParamNormalized(kParamPitchBend, 0.2);
    {
        MemoryStream stream;
        int32 written = 0;
        stream.write(const_cast<char*>(state.data()), static_cast<int32>(state.size()), &written);
        int64 start = 0;
        stream.seek(0, IBStream::kIBSeekSet, &start);
        CHECK(controller->setComponentState(&stream) == kResultOk);
    }
    for(int i = 0; i < kSavedParamCount; ++i)
        CHECK(controller->getParamNormalized(kFirstParam + i) == expected[i]);
    CHECK(controller->getParamNormalized(kParamPitchBend) == kDefaultPitchBend);
    {
        MemoryStream stream;
        CHECK(controller->setComponentState(&stream) != kResultOk); // empty
        CHECK(controller->getParamNormalized(kFirstParam) == expected[0]);
    }
    // Its own state: the window's zoom and the keyboard.
    {
        MemoryStream stream;
        CHECK(controller->getState(&stream) == kResultOk);
        int64 start = 0;
        stream.seek(0, IBStream::kIBSeekSet, &start);
        CHECK(controller->setState(&stream) == kResultOk);
        MemoryStream empty;
        CHECK(controller->setState(&empty) != kResultOk);
    }
    CHECK(controller->terminate() == kResultOk);
    std::printf("state: all 19 saved parameters come back exactly; a loaded state sounds like "
                "the same values set by hand; loading and activation end every note; damaged "
                "states are refused\n");
}

std::string Text(Controller& controller, ParamID id, double normalized) {
    String128 string{};
    CHECK(controller.getParamStringByValue(id, normalized, string) == kResultOk);
    return StringConvert::convert(string);
}

bool Typed(Controller& controller, ParamID id, const char* text, ParamValue& normalized) {
    String128 string{};
    StringConvert::convert(std::string(text), string);
    return controller.getParamValueByString(id, string, normalized) == kResultOk;
}

void TestController() {
    IPtr<Controller> controller = owned(new Controller());
    CHECK(controller->initialize(nullptr) == kResultOk);
    CHECK(controller->getParameterCount() == kParamCount);
    const struct {
        ParamID id;
        const char* title;
        int32 steps;
    } expected[] = {{100, "Contour", 0},      {101, "Bow", 0},        {102, "Bow Timbre", 0},
                    {103, "Blow", 0},         {104, "Flow", 0},       {105, "Blow Timbre", 0},
                    {106, "Strike", 0},       {107, "Mallet", 0},     {108, "Strike Timbre", 0},
                    {109, "Coarse", 60},      {110, "Fine", 0},       {111, "Geometry", 0},
                    {112, "Brightness", 0},   {113, "Damping", 0},    {114, "Position", 0},
                    {115, "Space", 0},        {116, "Freeze", 1},     {117, "Velocity", 0},
                    {118, "Volume", 0},       {119, "Pitch Bend", 0}};
    for(int i = 0; i < kParamCount; ++i) {
        ParameterInfo info{};
        CHECK(controller->getParameterInfo(i, info) == kResultOk);
        CHECK(info.id == expected[i].id && info.stepCount == expected[i].steps);
        CHECK(StringConvert::convert(info.title) == expected[i].title);
        CHECK(info.flags & ParameterInfo::kCanAutomate);
        CHECK(info.defaultNormalizedValue == kDefaults[i]);
        CHECK(controller->getParamNormalized(info.id) == kDefaults[i]);
    }
    // The defaults are the hardware's start-up patch.
    CHECK(kDefaults[Index(kParamStrikeLevel)] == 0.8 && kDefaults[Index(kParamGeometry)] == 0.2);
    CHECK(kDefaults[Index(kParamDamping)] == 0.25 && kDefaults[Index(kParamPosition)] == 0.3);

    // Values as text, and text as values.
    CHECK(Text(*controller, kParamCoarse, 0.5) == "0" && Text(*controller, kParamCoarse, 1.0) == "30");
    CHECK(Text(*controller, kParamCoarse, CoarseNormalized(-12)) == "-12");
    CHECK(Text(*controller, kParamFine, 0.5) == "0.00" && Text(*controller, kParamFine, 1.0) == "2.00");
    CHECK(Text(*controller, kParamVolume, 1.0) == "100.0" && Text(*controller, kParamGeometry, 0.2) == "0.200");
    CHECK(Text(*controller, kParamFreeze, 0.0) == "Off" && Text(*controller, kParamFreeze, 1.0) == "On");
    CHECK(Text(*controller, kParamPitchBend, 1.0) == "2.00" && Text(*controller, kParamPitchBend, 0.0) == "-2.00");
    ParamValue value = -1.0;
    CHECK(Typed(*controller, kParamCoarse, "7 st", value) && CoarseSemitones(value) == 7);
    CHECK(Typed(*controller, kParamCoarse, "-99", value) && CoarseSemitones(value) == -30);
    CHECK(Typed(*controller, kParamFine, "-1", value) && std::fabs(FineSemitones(value) + 1.0) < 1e-12);
    CHECK(Typed(*controller, kParamVolume, "50 %", value) && value == 0.5);
    CHECK(Typed(*controller, kParamFreeze, "on", value) && value == 1.0);
    CHECK(Typed(*controller, kParamGeometry, "0,25", value) && value == 0.25);
    value = 0.123;
    CHECK(!Typed(*controller, kParamGeometry, "foo", value) && !Typed(*controller, kParamCoarse, "12junk", value));
    CHECK(!Typed(*controller, kParamFreeze, "maybe", value) && value == 0.123);

    // MIDI: the wheel and six controllers, on every channel of bus 0.
    ParamID id = 0;
    for(int16 channel = 0; channel < 16; ++channel) {
        CHECK(controller->getMidiControllerAssignment(0, channel, kPitchBend, id) == kResultTrue
              && id == kParamPitchBend);
        CHECK(controller->getMidiControllerAssignment(0, channel, kMidiCcBrightness, id) == kResultTrue
              && id == kParamBrightness);
        CHECK(controller->getMidiControllerAssignment(0, channel, kMidiCcSpace, id) == kResultTrue
              && id == kParamSpace);
    }
    CHECK(controller->getMidiControllerAssignment(0, 0, kCtrlModWheel, id) == kResultFalse);
    CHECK(controller->getMidiControllerAssignment(1, 0, kPitchBend, id) == kResultFalse);
    CHECK(controller->getMidiControllerAssignment(0, 16, kPitchBend, id) == kResultFalse);

    // The status line.
    EngineStatus status;
    CHECK(StatusText(status) == "Engine 48 kHz native");
    status = {true, 48000.0, 48000.0, 0};
    CHECK(StatusText(status) == "Engine 48 kHz native  |  Host 48 kHz, no conversion");
    status = {true, 44100.0, 48000.0, 49};
    CHECK(StatusText(status) == "Engine 48 kHz native  |  Host 44.1 kHz, converted, latency 49 samples");
    CHECK(controller->terminate() == kResultOk);
    std::printf("controller: 20 parameters with the IDs 100..119, typed values read strictly, "
                "pitch wheel and six MIDI controllers assigned\n");
}

void TestKeyboardAndStatus() {
    // Processor and controller connected as a host connects them.
    IPtr<HostApplication> application = owned(new HostApplication());
    IPtr<Processor> processor = owned(new Processor());
    IPtr<Controller> controller = owned(new Controller());
    CHECK(processor->initialize(application) == kResultOk);
    CHECK(controller->initialize(application) == kResultOk);
    CHECK(processor->connect(controller) == kResultOk);
    CHECK(controller->connect(processor) == kResultOk);
    CHECK(!controller->Status().known);
    ProcessSetup setup{kRealtime, kSample32, 64, 44100.0};
    CHECK(processor->setupProcessing(setup) == kResultOk);
    CHECK(processor->setActive(true) == kResultOk);
    // On activation the processor reports its rates for the status line.
    CHECK(controller->Status().known && controller->Status().host_rate == 44100.0);
    CHECK(controller->Status().engine_rate == 48000.0);
    CHECK(controller->Status().latency == static_cast<int>(processor->getLatencySamples()));
    CHECK(controller->Status().latency > 0);
    const int reported_latency = controller->Status().latency;

    HostProcessData data;
    CHECK(data.prepare(*processor.get(), 64, kSample32));
    EventList out(64);
    data.outputEvents = &out;
    data.numSamples = 64;
    double energy = 0.0;
    const auto block = [&] {
        out.clear();
        CHECK(processor->process(data) == kResultOk);
        std::string sent;
        for(int32 i = 0; i < out.getEventCount(); ++i) {
            Event event{};
            CHECK(out.getEvent(i, event) == kResultOk);
            CHECK(event.busIndex == 0 && event.sampleOffset == 0);
            if(event.type == Event::kNoteOnEvent) {
                CHECK(std::fabs(event.noteOn.velocity - kKeyboardVelocity / 127.0f) < 1e-6f);
                sent += "N" + std::to_string(event.noteOn.pitch) + " ";
            } else {
                CHECK(event.type == Event::kNoteOffEvent);
                sent += "F" + std::to_string(event.noteOff.pitch) + " ";
            }
        }
        for(int i = 0; i < 64; ++i)
            energy += data.outputs[0].channelBuffers32[1][i] * data.outputs[0].channelBuffers32[1][i];
        return sent;
    };
    CHECK(block().empty() && energy == 0.0);
    // A key of the screen keyboard: through the controller, as a message.
    CHECK(controller->PlayNote(64));
    CHECK(block() == "N64 ");
    for(int i = 0; i < 100; ++i)
        CHECK(block().empty());
    CHECK(processor->SynthEngine().Gate() && processor->SynthEngine().SoundingNote() == 64);
    CHECK(energy > 1e-4);
    // Letting go ends the host's note and closes the gate.
    CHECK(controller->ReleaseNote(64));
    CHECK(block() == "F64 ");
    for(int i = 0; i < 4; ++i)
        block();
    CHECK(!processor->SynthEngine().Gate());
    CHECK(!controller->PlayNote(128) && !controller->PlayNote(-1));
    // The same key twice without a release in between: the first note is ended first.
    CHECK(SendNote(*processor, 62) == kResultOk && SendNote(*processor, 62) == kResultOk);
    CHECK(block() == "N62 F62 N62 ");
    // Nonsense is refused.
    CHECK(SendNote(*processor, 128) != kResultOk && SendNote(*processor, 60, 128) != kResultOk);
    CHECK(SendNote(*processor, 60, 100, "SomethingElse") != kResultOk);
    // A key still held when the plug-in is deactivated: the host gets its
    // note-off at the next block, and the engine starts silent.
    CHECK(processor->setActive(false) == kResultOk);
    CHECK(processor->setActive(true) == kResultOk);
    CHECK(block() == "F62 ");
    CHECK(!processor->SynthEngine().Gate() && processor->SynthEngine().HeldCount() == 0);
    // Keys pressed while nobody renders are not played later.
    CHECK(SendNote(*processor, 70) == kResultOk);
    std::this_thread::sleep_for(Processor::kKeyboardNoteLifetime + std::chrono::milliseconds(150));
    CHECK(block().empty() && !processor->SynthEngine().Gate());
    // A status message with nonsense in it is refused.
    {
        auto message = owned(new HostMessage());
        message->setMessageID(kStatusMessageId);
        message->getAttributes()->setFloat(kStatusHostRate, -1.0);
        message->getAttributes()->setFloat(kStatusEngineRate, 48000.0);
        message->getAttributes()->setInt(kStatusLatency, 0);
        CHECK(controller->notify(message) != kResultOk);
        CHECK(controller->Status().host_rate == 44100.0);
    }
    processor->setActive(false);
    processor->disconnect(controller);
    controller->disconnect(processor);
    CHECK(processor->terminate() == kResultOk && controller->terminate() == kResultOk);

    // The keyboard's geometry: 25 keys from C4, each found where it is drawn.
    for(int note = kKeyboardLowNote; note <= kKeyboardHighNote; ++note) {
        const KeyRect key = KeyRectFor(note, 700.0, 60.0);
        CHECK(NoteAt((key.left + key.right) / 2, key.top + 2.0, 700.0, 60.0) == note);
    }
    CHECK(NoteAt(-1.0, 5.0, 700.0, 60.0) == -1 && NoteAt(5.0, 61.0, 700.0, 60.0) == -1);
    std::printf("screen keyboard and status: keys reach the engine and the MIDI output, down and "
                "up; the controller learns the rates on activation (44.1 kHz host: latency %d)\n",
                reported_latency);
}

// The parameter values between the threads (after Torus Classic): a loaded
// state replaces everything; automation written for an older state does not
// leak into a newer one.
void TestParameterState() {
    ParameterState state;
    ParameterState::Values values = state.Current();
    for(int i = 0; i < kParamCount; ++i)
        CHECK(values[i] == kDefaults[i]);
    state.Write(Index(kParamGeometry), 0.9);
    CHECK(state.Current()[Index(kParamGeometry)] == 0.9);
    SavedValues saved{};
    saved.fill(0.25);
    state.Load(saved);
    // Loaded, not yet adopted: the automation of the old state no longer counts.
    values = state.Current();
    CHECK(values[Index(kParamGeometry)] == 0.25 && values[Index(kParamPitchBend)] == kDefaultPitchBend);
    state.Write(Index(kParamGeometry), 0.7); // still tagged with the old generation
    CHECK(state.Current()[Index(kParamGeometry)] == 0.25);
    ParameterState::Loaded loaded;
    CHECK(state.TakeLoaded(loaded) && !state.TakeLoaded(loaded));
    state.Adopt(loaded.generation);
    state.Write(Index(kParamGeometry), 0.6);
    CHECK(state.Current()[Index(kParamGeometry)] == 0.6);
    std::printf("parameter state: a loaded state replaces everything; older automation does not "
                "leak into it\n");
}

} // namespace

int main() {
    TestBuses();
    TestParameterState();
    TestController();
    TestNotes();
    TestAutomation();
    TestState();
    TestKeyboardAndStatus();
    TestBlockSizesAndRates();
    std::printf("PASS\n");
    return 0;
}
