// Tests of Torus Classic through its VST3 interfaces: the processor against
// the pinned firmware engine, block sizes and sample formats, sample rates,
// several instances, life cycle and state, the controller's parameters and
// MIDI map, and the editor in a real window with a host stand-in.

#define TEST_SUPPORT_COUNT_ALLOCATIONS
#include "test_support.h"

#include "controller.h"
#include "editor.h"
#include "parameter_state.h"
#include "parameters.h"
#include "processor.h"
#include "reference_api.h"
#include "value_text.h"
#include "view_layout.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/cknob.h"

#include <windows.h>
#include <xmmintrin.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace torus_classic;
using namespace test_support;
using torus_classic_reference::FirmwareSynth;

// In a plug-in the SDK's DLL entry point defines this handle and calls
// InitModule(). The test links the plug-in's classes into an executable, so it
// has to provide both itself (see TestEditorInWindow).
void* moduleHandle = nullptr;
bool InitModule();
bool DeinitModule();

namespace {

constexpr int kQ = DryEngine::kQuantum;
constexpr int kBlockSizes[] = {1, 7, 12, 16, 48, 64, 333, 1024, 2048};
constexpr double kRates[] = {44100.0, 48000.0, 88200.0, 96000.0, 192000.0};
constexpr int kVaryingBlocks[] = {64, 1, 333, 12, 7, 2048, 16, 48, 5, 1024, 13, 256, 3, 11, 480};

// --- A minimal host around one processor ------------------------------------

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
Ev Param(std::int64_t sample, ParamID id, double value) {
    return {sample, Ev::kParam, static_cast<int>(id), value};
}

struct HostConfig {
    double rate = 48000.0;
    int max_block = 2048;
    bool double_precision = false;
    int32 mode = kRealtime;
};

class PluginHost {
  public:
    explicit PluginHost(const HostConfig& config, HostScore score = {})
    : config_(config), score_(std::move(score)), events_(4096), changes_(kParamCount) {
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
        ProcessSetup setup{config_.mode, size, config_.max_block, rate};
        CHECK(processor->setupProcessing(setup) == kResultOk);
        data_.unprepare();
        CHECK(data_.prepare(*processor.get(), config_.max_block, size));
        CHECK(data_.numOutputs == 1 && data_.outputs[0].numChannels == 2);
        data_.processMode = config_.mode;
        data_.inputEvents = &events_;
        data_.inputParameterChanges = &changes_;
    }

    void AddScore(const HostScore& more) { score_.insert(score_.end(), more.begin(), more.end()); }

    // One process() call of `frames` samples, with the score's events of
    // that span. `frames` may be 0: a parameter flush.
    void Process(int frames) {
        CHECK(frames <= config_.max_block);
        events_.clear();
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
        bool silent = true;
        for(int i = 0; i < frames; ++i) {
            const double l = config_.double_precision ? data_.outputs[0].channelBuffers64[0][i]
                                                      : data_.outputs[0].channelBuffers32[0][i];
            const double r = config_.double_precision ? data_.outputs[0].channelBuffers64[1][i]
                                                      : data_.outputs[0].channelBuffers32[1][i];
            CHECK(std::isfinite(l) && std::isfinite(r));
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

    template <typename NextBlock>
    void Run(std::int64_t total, NextBlock next_block) {
        const std::int64_t end = position + total;
        while(position < end)
            Process(static_cast<int>(std::min<std::int64_t>(
                std::min<std::int64_t>(next_block(), config_.max_block), end - position)));
    }
    void Run(std::int64_t total, int block = 64) {
        Run(total, [block] { return block; });
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
    std::int64_t position = 0;
    std::uint64_t allocations = 0;

  private:
    HostConfig config_;
    HostScore score_;
    std::size_t next_ = 0;
    HostProcessData data_;
    EventList events_;
    ParameterChanges changes_;
};

struct VaryingBlocks {
    std::size_t at = 0;
    int operator()() { return kVaryingBlocks[at++ % std::size(kVaryingBlocks)]; }
};

// A performance in host samples at `rate`: notes on and around block and
// quantum boundaries, chords denser than four per quantum, every parameter
// moving, bends and model changes, note-offs in between.
HostScore Performance(double rate, std::int64_t total, std::uint32_t seed) {
    HostScore score;
    Lcg random(seed);
    const auto scaled = [rate](std::int64_t at_48k) {
        return static_cast<std::int64_t>(at_48k * rate / 48000.0);
    };
    for(std::int64_t t : {0, 1, 11, 12, 13, 47, 48, 63, 64, 65, 332, 333, 334, 1023, 1024, 2047,
                          2048, 2049})
        score.push_back(NoteOn(t, 48 + static_cast<int>(t % 36), 100));
    for(int i = 0; i < 9; ++i)
        score.push_back(NoteOn(scaled(3000), 40 + 4 * i, 110));
    std::int64_t t = scaled(3500);
    while(true) {
        t += 1 + static_cast<std::int64_t>(random.Below(1400) * rate / 48000.0);
        if(t >= total)
            break;
        const std::uint32_t kind = random.Below(12);
        if(kind < 5) {
            const std::uint32_t count = 1 + random.Below(4);
            for(std::uint32_t i = 0; i < count; ++i)
                score.push_back(NoteOn(t, static_cast<int>(random.Below(128)),
                                       1 + static_cast<int>(random.Below(127))));
        } else if(kind < 6) {
            score.push_back({t, Ev::kNoteOff, static_cast<int>(random.Below(128)), 0.5});
        } else if(kind < 10) {
            static constexpr ParamID kMoving[] = {kParamTune,     kParamStructure, kParamBrightness,
                                                  kParamDamping,  kParamPosition,  kParamExcitation,
                                                  kParamVolume};
            score.push_back(Param(t, kMoving[random.Below(7)], random.Unit()));
        } else if(kind < 11) {
            score.push_back(Param(t, kParamPitchBend, random.Unit()));
        } else if(random.Below(3) == 0) {
            score.push_back(Param(t, kParamModel, random.Unit()));
        }
    }
    return score;
}

// --- The processor against the firmware engine, at 48 kHz --------------------
//
// At 48 kHz the adapter is a FIFO, so the plug-in must deliver the firmware
// engine's dry output with every event applied on the first quantum at or
// after its sample. The firmware engine is prepared as in the engine test
// (Dry/Wet set to 0, then silence until its smoothing has reached exactly 0);
// the plug-in renders the same span of silence.
//
// Tolerance, fixed before the first run. The plug-in renders with denormal
// numbers flushed to zero (see StreamAdapter). With the firmware engine put
// in the same mode, every sample must compare equal (==). Left in the default
// mode it may differ where one of them holds a denormal: by sums of values
// below FLT_MIN (1.2e-38); the bound used is 1e-30.

constexpr std::int64_t kWarmUpQuanta = 400;

class FlushDenormals {
  public:
    explicit FlushDenormals(bool on) : saved_(_mm_getcsr()) {
        if(on)
            _mm_setcsr(saved_ | 0x8040u);
    }
    ~FlushDenormals() { _mm_setcsr(saved_); }

  private:
    unsigned int saved_;
};

std::vector<float> RenderFirmware(const HostScore& score, std::int64_t total, bool flush) {
    using namespace torus_classic_reference;
    const FlushDenormals mode(flush);
    FirmwareSynth firmware;
    firmware.Reset(DryEngine::kDefaultSeed);
    firmware.Set(kWet, 0.0f);
    float l[kQ], r[kQ];
    for(std::int64_t q = 0; q < kWarmUpQuanta; ++q)
        firmware.Render(l, r);
    CHECK(firmware.Current(kWet) == 0.0f);
    std::vector<float> left, right;
    std::size_t next = 0;
    for(std::int64_t quantum = 0; quantum * kQ < total; ++quantum) {
        // The events of this quantum: those at samples (quantum-1)*12+1 .. quantum*12.
        for(; next < score.size() && (score[next].sample + kQ - 1) / kQ <= quantum; ++next) {
            const Ev& ev = score[next];
            if(ev.kind == Ev::kNoteOn) {
                firmware.NoteOn(static_cast<unsigned>(ev.a),
                                static_cast<unsigned>(std::lround(ev.value * 127.0)));
            } else if(ev.kind == Ev::kParam) {
                const auto value = static_cast<float>(ev.value);
                switch(ev.a) {
                    case kParamTune: firmware.Set(kFrequency, value); break;
                    case kParamStructure: firmware.Set(kStructure, value); break;
                    case kParamBrightness: firmware.Set(kBrightness, value); break;
                    case kParamDamping: firmware.Set(kDamping, value); break;
                    case kParamPosition: firmware.Set(kPosition, value); break;
                    case kParamExcitation: firmware.Set(kExcitation, value); break;
                    case kParamVolume: firmware.Set(kMaster, value); break;
                    case kParamModel:
                        firmware.SelectModel(static_cast<unsigned>(ModelIndex(ev.value)));
                        break;
                    case kParamPitchBend:
                        firmware.SetBend(static_cast<unsigned>(PitchBendRaw(ev.value)));
                        break;
                    default: break;
                }
            } // note-off: the firmware does nothing
        }
        firmware.Render(l, r);
        left.insert(left.end(), l, l + kQ);
        right.insert(right.end(), r, r + kQ);
    }
    left.resize(static_cast<std::size_t>(total));
    right.resize(static_cast<std::size_t>(total));
    left.insert(left.end(), right.begin(), right.end());
    return left;
}

void TestAgainstFirmware() {
    const std::int64_t warm_up = kWarmUpQuanta * kQ;
    std::size_t compared = 0;
    double worst_unflushed = 0.0;
    for(const std::uint32_t seed : {1u, 2u, 3u}) {
#ifdef NDEBUG
        const std::int64_t total = 20 * 48000;
#else
        const std::int64_t total = 3 * 48000;
#endif
        HostScore score = Performance(48000.0, total, seed);
        // One model each as the starting point, so all six are covered.
        score.insert(score.begin(), Param(0, kParamModel, ModelNormalized(static_cast<int>(seed) * 2 - 1)));
        HostScore shifted = score; // the plug-in plays it after the warm-up
        for(Ev& ev : shifted)
            ev.sample += warm_up;
        PluginHost host({48000.0, 2048, false, kRealtime}, shifted);
        host.Run(warm_up, 64);
        CHECK(Energy(host.Stereo()) == 0.0);
        host.Clear();
        host.Run(total, VaryingBlocks{});
        const auto plugin = host.Stereo();
        CHECK(Energy(plugin) > 1.0);
        CHECK(Peak(plugin) < 0.8f);

        const auto firmware = RenderFirmware(score, total, true);
        CHECK(plugin.size() == firmware.size());
        std::size_t different = 0;
        for(std::size_t i = 0; i < plugin.size(); ++i)
            different += plugin[i] != firmware[i];
        CHECK(different == 0);

        const auto unflushed = RenderFirmware(score, total, false);
        for(std::size_t i = 0; i < plugin.size(); ++i)
            worst_unflushed =
                std::max(worst_unflushed, std::fabs(double(plugin[i]) - double(unflushed[i])));
        CHECK(worst_unflushed <= 1e-30);
        compared += plugin.size();
        CHECK(host.allocations == 0);
    }
    std::printf("firmware comparison at 48 kHz: %zu samples equal to the pinned firmware engine "
                "(denormals flushed in both); against the firmware's default float mode the "
                "largest difference is %.3g\n",
                compared, worst_unflushed);
}

// --- One absolute event list, any block size, both sample formats -------------

void TestBlockSizes() {
    for(const double rate : kRates) {
#ifdef NDEBUG
        const auto total = static_cast<std::int64_t>(1.5 * rate);
#else
        const auto total = static_cast<std::int64_t>(0.25 * rate);
#endif
        const HostScore score = Performance(rate, total, 77);
        std::vector<float> reference;
        int variants = 0;
        for(const bool double_precision : {false, true}) {
            for(int variant = 0; variant <= static_cast<int>(std::size(kBlockSizes)); ++variant) {
                PluginHost host({rate, 2048, double_precision, kRealtime}, score);
                if(variant < static_cast<int>(std::size(kBlockSizes)))
                    host.Run(total, kBlockSizes[variant]);
                else
                    host.Run(total, VaryingBlocks{});
                CHECK(host.allocations == 0);
                const auto out = host.Stereo();
                if(reference.empty()) {
                    reference = out;
                    CHECK(Energy(reference) > 1.0);
                }
                CHECK(out == reference);
                ++variants;
            }
        }
        std::printf("  %g Hz: %zu events over %lld samples, %d renderings identical (RMS %.4f, "
                    "peak %.4f)\n",
                    rate, score.size(), static_cast<long long>(total), variants, Rms(reference),
                    Peak(reference));
    }
    std::printf("block sizes: blocks of 1, 7, 12, 16, 48, 64, 333, 1024, 2048 and changing "
                "lengths, 32 and 64 bit, give the same samples at all five rates\n");
}

// --- Notes as hosts send them ------------------------------------------------

void TestNoteHandling() {
    const std::int64_t total = 24000;
    const auto render = [&](const HostScore& score) {
        PluginHost host({48000.0, 2048, false, kRealtime}, score);
        host.Run(total, 64);
        return host.Stereo();
    };
    const auto struck = render({NoteOn(100, 60, 100)});
    CHECK(Energy(struck) > 0.1);
    // A note-off, and a note-on with velocity 0, strike nothing and stop nothing.
    CHECK(render({NoteOn(100, 60, 100), {5000, Ev::kNoteOff, 60, 0.0}}) == struck);
    CHECK(render({NoteOn(100, 60, 100), {5000, Ev::kNoteOn, 60, 0.0}}) == struck);
    CHECK(Energy(render({{100, Ev::kNoteOn, 60, 0.0}, {200, Ev::kNoteOff, 60, 1.0}})) == 0.0);
    // The tail is still there long after the note-off.
    CHECK(Energy(std::vector<float>(struck.begin() + 20000, struck.begin() + 24000)) > 0.0);
    // Velocity maps to 1..127: the smallest positive velocity is 1, above 1.0 is 127.
    CHECK(render({{100, Ev::kNoteOn, 60, 1e-6}}) == render({NoteOn(100, 60, 1)}));
    CHECK(render({{100, Ev::kNoteOn, 60, 7.5}}) == render({NoteOn(100, 60, 127)}));
    // Not-a-number and infinite velocities and pitches outside 0..127 are nothing.
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    CHECK(Energy(render({{100, Ev::kNoteOn, 60, nan}, {100, Ev::kNoteOn, 60, -inf},
                         {100, Ev::kNoteOn, -1, 1.0}, {100, Ev::kNoteOn, 128, 1.0},
                         {100, Ev::kNoteOn, 60, -0.5}}))
          == 0.0);
    // The ends of the range sound.
    CHECK(Energy(render({NoteOn(100, 0, 127)})) > 0.0);
    CHECK(Energy(render({NoteOn(100, 127, 127)})) > 0.0);

    // Offsets a host should not send: before the block, behind it, out of
    // order. They are pulled into the block and into order.
    const auto raw = [&](std::initializer_list<std::pair<int32, int>> notes) {
        IPtr<Processor> processor = owned(new Processor());
        CHECK(processor->initialize(nullptr) == kResultOk);
        ProcessSetup setup{kRealtime, kSample32, 64, 48000.0};
        CHECK(processor->setupProcessing(setup) == kResultOk);
        CHECK(processor->setActive(true) == kResultOk);
        HostProcessData data;
        CHECK(data.prepare(*processor.get(), 64, kSample32));
        EventList events(16);
        data.inputEvents = &events;
        data.numSamples = 64;
        std::vector<float> out;
        for(int block = 0; block < 100; ++block) {
            events.clear();
            if(block == 1)
                for(const auto& [offset, pitch] : notes) {
                    Event event{};
                    event.type = Event::kNoteOnEvent;
                    event.sampleOffset = offset;
                    event.noteOn.pitch = static_cast<int16>(pitch);
                    event.noteOn.velocity = 100.0f / 127.0f;
                    CHECK(events.addEvent(event) == kResultOk);
                }
            CHECK(processor->process(data) == kResultOk);
            out.insert(out.end(), data.outputs[0].channelBuffers32[0],
                       data.outputs[0].channelBuffers32[0] + 64);
        }
        processor->setActive(false);
        processor->terminate();
        return out;
    };
    CHECK(raw({{-500, 60}}) == raw({{0, 60}}));
    CHECK(raw({{100000, 60}}) == raw({{63, 60}}));
    CHECK(raw({{40, 60}, {10, 64}, {std::numeric_limits<int32>::min(), 67}})
          == raw({{40, 60}, {40, 64}, {40, 67}}));
    CHECK(Energy(raw({{std::numeric_limits<int32>::max(), 60}})) > 0.0);
    std::printf("notes: note-off and velocity 0 neither strike nor stop anything; velocity "
                "1..127; invalid pitches, velocities and offsets handled\n");
}

// --- Other sample rates: the same sound, delayed by the reported latency ------

void TestSampleRates() {
    // Acceptance, fixed before the first run: where the 48 kHz rendering has a
    // spectral component within 60 dB of its strongest, the rendering at
    // another rate shows it within 0.02 dB, measured over the same span of
    // time (shifted by the reported latency). The filter's passband ripple is
    // below 0.0001 dB; the rest of the margin is for the measurement itself.
    constexpr double kToleranceDb = 0.02;
    std::printf("  model                rate     latency  components  worst difference\n");
    for(int model = 0; model < kModelCount; ++model) {
        std::vector<std::vector<float>> spectra_left;
        std::vector<double> frequencies;
        for(double hz = 90.0; hz < 15000.0; hz *= 1.085)
            frequencies.push_back(hz);
        std::vector<double> reference;
        for(const double rate : {48000.0, 44100.0, 88200.0, 96000.0, 192000.0}) {
            const auto at = [rate](double seconds) {
                return static_cast<std::int64_t>(std::llround(seconds * rate));
            };
            HostScore score = {Param(0, kParamModel, ModelNormalized(model)),
                               Param(0, kParamDamping, 0.7), Param(0, kParamBrightness, 0.6)};
            for(int pitch : {45, 52, 57, 61})
                score.push_back(NoteOn(at(0.02), pitch, 110));
            score.push_back(Param(at(0.3), kParamPitchBend, 0.85));
            score.push_back(Param(at(0.5), kParamStructure, 0.3));
            PluginHost host({rate, 2048, false, kRealtime}, score);
            const auto latency = static_cast<std::int64_t>(host.processor->getLatencySamples());
            CHECK(latency == host.processor->Adapter().LatencySamples());
            host.Run(at(1.0) + latency, VaryingBlocks{});
            const auto from = static_cast<std::size_t>(at(0.1) + latency);
            const auto to = static_cast<std::size_t>(at(0.9) + latency);
            std::vector<double> amplitudes;
            for(const double hz : frequencies)
                amplitudes.push_back(ToneAmplitude(host.left, hz, rate, from, to)
                                     + ToneAmplitude(host.right, hz, rate, from, to));
            if(rate == 48000.0) {
                CHECK(latency == 0);
                reference = amplitudes;
                continue;
            }
            const double strongest = *std::max_element(reference.begin(), reference.end());
            CHECK(strongest > 1e-3);
            double worst = 0.0;
            int counted = 0;
            for(std::size_t i = 0; i < frequencies.size(); ++i) {
                if(reference[i] < strongest * 1e-3)
                    continue;
                worst = std::max(worst, std::fabs(Decibel(amplitudes[i] / reference[i])));
                ++counted;
            }
            std::printf("  %-20s %-8g %5lld    %3d         %.5f dB\n", kModelNames[model], rate,
                        static_cast<long long>(latency), counted, worst);
            CHECK(counted >= 10);
            CHECK(worst <= kToleranceDb);
        }
    }
    // The latency the host is told, per rate.
    const std::pair<double, uint32> expected[] = {
        {44100.0, 49}, {48000.0, 0}, {88200.0, 90}, {96000.0, 97}, {192000.0, 195}};
    for(const auto& [rate, latency] : expected) {
        PluginHost host({rate, 2048, false, kRealtime});
        CHECK(host.processor->getLatencySamples() == latency);
        CHECK(host.processor->getTailSamples() == kInfiniteTail);
    }
    // Rates outside the supported range are refused at setup.
    IPtr<Processor> processor = owned(new Processor());
    CHECK(processor->initialize(nullptr) == kResultOk);
    for(const double bad : {0.0, -1.0, 500.0, 1e9, std::numeric_limits<double>::quiet_NaN()}) {
        ProcessSetup setup{kRealtime, kSample32, 64, bad};
        CHECK(processor->setupProcessing(setup) != kResultOk);
    }
    CHECK(processor->canProcessSampleSize(kSample32) == kResultTrue
          && processor->canProcessSampleSize(kSample64) == kResultTrue
          && processor->canProcessSampleSize(7) != kResultTrue);
    SpeakerArrangement stereo = SpeakerArr::kStereo, mono = SpeakerArr::kMono;
    CHECK(processor->setBusArrangements(nullptr, 0, &stereo, 1) == kResultTrue);
    CHECK(processor->setBusArrangements(nullptr, 0, &mono, 1) != kResultTrue);
    CHECK(processor->setBusArrangements(&stereo, 1, &stereo, 1) != kResultTrue); // no audio input
    processor->terminate();
    std::printf("sample rates: all six models keep their spectrum at 44.1, 88.2, 96 and 192 "
                "kHz within %.2f dB of the 48 kHz rendering; latency 49/0/90/97/195 samples\n",
                kToleranceDb);
}

// --- Two instances ------------------------------------------------------------

void TestInstances() {
    const double rate = 48000.0;
    const std::int64_t total = 2 * 48000;
    HostScore a_score = Performance(rate, total, 5);
    a_score.insert(a_score.begin(), Param(0, kParamModel, ModelNormalized(2))); // noisy strings
    const HostScore b_score = Performance(96000.0, 2 * 96000, 6);
    const auto alone = [&] {
        PluginHost a({rate, 2048, false, kRealtime}, a_score);
        a.Run(total, VaryingBlocks{});
        return a.Stereo();
    }();
    CHECK(Energy(alone) > 1.0);

    // Interleaved on one thread; B at another rate, restarted, destroyed and
    // created again between A's blocks.
    {
        PluginHost a({rate, 2048, false, kRealtime}, a_score);
        auto b = std::make_unique<PluginHost>(HostConfig{96000.0, 2048, true, kRealtime}, b_score);
        VaryingBlocks blocks;
        int step = 0;
        while(a.position < total) {
            a.Process(static_cast<int>(std::min<std::int64_t>(blocks(), total - a.position)));
            b->Process(480);
            if(++step % 23 == 0) {
                CHECK(b->processor->setActive(false) == kResultOk);
                b->Setup(step % 2 ? 44100.0 : 192000.0);
                CHECK(b->processor->setActive(true) == kResultOk);
            }
            if(step % 61 == 0)
                b = std::make_unique<PluginHost>(HostConfig{88200.0, 2048, false, kRealtime},
                                                 b_score);
        }
        CHECK(a.Stereo() == alone);
    }

    // In parallel: other threads create, play, load states into, restart and
    // destroy instances while A renders.
    {
        std::atomic<bool> stop{false};
        std::atomic<int> instances{0};
        const auto churn = [&](std::uint32_t seed) {
            Lcg random(seed);
            while(!stop.load()) {
                const double other_rate = kRates[random.Below(5)];
                PluginHost b({other_rate, 2048, random.Below(2) == 0, kRealtime},
                             Performance(other_rate, 20000, random.Next()));
                b.Run(8000, 256);
                CHECK(b.processor->setActive(false) == kResultOk);
                CHECK(b.processor->setActive(true) == kResultOk);
                b.Run(4000, 64);
                instances.fetch_add(1);
            }
        };
        std::thread t1(churn, 1u), t2(churn, 2u), t3(churn, 3u);
        int renderings = 0;
        while(instances.load() < 12 || renderings < 2) {
            PluginHost a({rate, 2048, false, kRealtime}, a_score);
            a.Run(total, VaryingBlocks{});
            CHECK(a.Stereo() == alone);
            ++renderings;
        }
        stop.store(true);
        t1.join();
        t2.join();
        t3.join();
        std::printf("  parallel: %d other instances lived and died during %d renderings of A\n",
                    instances.load(), renderings);
    }

    // Offline, and A's blocks handed from thread to thread.
    {
        PluginHost a({rate, 2048, false, kOffline}, a_score);
        VaryingBlocks blocks;
        while(a.position < total) {
            std::thread worker([&] {
                for(int i = 0; i < 20 && a.position < total; ++i)
                    a.Process(static_cast<int>(
                        std::min<std::int64_t>(blocks(), total - a.position)));
            });
            worker.join();
        }
        CHECK(a.Stereo() == alone);
    }
    // Offline order: all of A then all of B, or the other way round.
    {
        PluginHost b({rate, 2048, false, kOffline}, b_score);
        b.Run(total, 1024);
        PluginHost a({rate, 2048, false, kOffline}, a_score);
        a.Run(total, 1024);
        CHECK(a.Stereo() == alone);
    }
    std::printf("instances: A sounds the same alone, interleaved with B, beside threads that "
                "create, restart and destroy instances, offline and across threads\n");
}

// --- Life cycle ---------------------------------------------------------------

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

tresult LoadState(IComponent& component, const std::vector<char>& bytes) {
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
        std::memcpy(bytes.data() + sizeof(version), values.data(),
                    values.size() * sizeof(double));
    return bytes;
}

// The parameter values between the threads, step by step: the orderings a
// test cannot force on real threads (review finding TC-R01 and the
// setActive path).
void TestParameterState() {
    const auto is = [](const ParameterState::Values& values, double all, double bend) {
        for(int i = 0; i < kSavedParamCount; ++i)
            if(values[i] != all)
                return false;
        return values[kParamPitchBend - kFirstParam] == bend;
    };
    const auto saved = [](double value) {
        std::array<double, kSavedParamCount> values;
        values.fill(value);
        return values;
    };
    ParameterState state;
    ParameterState::Values current = state.Current();
    for(int i = 0; i < kParamCount; ++i)
        CHECK(current[i] == kDefaults[i]);
    ParameterState::Loaded loaded;
    CHECK(!state.TakeLoaded(loaded));

    // Automation before any state was loaded counts.
    state.Write(3, 0.25);
    state.Write(8, 0.9);
    CHECK(state.Current()[3] == 0.25 && state.Current()[8] == 0.9 && state.Current()[0] == 0.5);

    // A loaded state replaces everything at once, at once: the automation
    // before it is history, the pitch wheel is back in its centre.
    state.Load(saved(0.1));
    CHECK(is(state.Current(), 0.1, kDefaultPitchBend));

    // Automation of a block that was already running when the state came:
    // the audio thread has not taken the state yet, so the value belongs to
    // the old one and must not show.
    state.Write(2, 0.77);
    state.Write(8, 0.3);
    CHECK(is(state.Current(), 0.1, kDefaultPitchBend));

    // The audio thread takes the state: complete in itself, once.
    CHECK(state.TakeLoaded(loaded) && is(loaded.values, 0.1, kDefaultPitchBend));
    CHECK(!state.TakeLoaded(loaded));
    state.Adopt(loaded.generation);
    CHECK(is(state.Current(), 0.1, kDefaultPitchBend)); // the old values stay history
    // From now on its automation counts.
    state.Write(2, 0.66);
    current = state.Current();
    CHECK(current[2] == 0.66 && current[1] == 0.1 && current[3] == 0.1);

    // Two states before the audio thread looks again: it gets the newest, and
    // what it wrote meanwhile belongs to neither.
    state.Load(saved(0.2));
    state.Load(saved(0.3));
    state.Write(4, 0.99);
    CHECK(is(state.Current(), 0.3, kDefaultPitchBend));
    CHECK(state.TakeLoaded(loaded) && is(loaded.values, 0.3, kDefaultPitchBend));
    CHECK(!state.TakeLoaded(loaded));
    // Still not adopted (as in setActive before the restart): the loaded state alone.
    CHECK(is(state.Current(), 0.3, kDefaultPitchBend));
    state.Adopt(loaded.generation);
    state.Write(4, 0.5);
    CHECK(state.Current()[4] == 0.5 && state.Current()[5] == 0.3);
    state.Write(-1, 0.7); // no such parameters
    state.Write(kParamCount, 0.7);
    CHECK(state.Current()[0] == 0.3);

    // One thread writing, one reading: a read always returns a pair that was
    // written, never half of one.
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        double value = 0.0;
        while(!stop.load()) {
            value = value >= 0.999 ? 0.0 : value + 0.001;
            for(int i = 0; i < kSavedParamCount; ++i)
                state.Write(i, value);
        }
    });
    for(int i = 0; i < 200000; ++i) {
        current = state.Current();
        for(int k = 0; k < kSavedParamCount; ++k)
            CHECK(current[k] >= 0.0 && current[k] <= 1.0);
    }
    stop.store(true);
    writer.join();
    std::printf("parameter state: a loaded state replaces everything at once; automation "
                "counts only for the state the audio thread has taken\n");
}

void TestKeyboardQueue() {
    PluginHost host({48000.0, 2048, false, kRealtime});
    Processor& processor = *host.processor;
    // A struck key sounds from the next block on, like a note-on at its first sample.
    CHECK(SendNote(processor, 64) == kResultOk);
    host.Run(9600);
    PluginHost midi({48000.0, 2048, false, kRealtime}, {NoteOn(0, 64, kKeyboardVelocity)});
    midi.Run(9600);
    CHECK(host.Stereo() == midi.Stereo() && Energy(host.Stereo()) > 0.01);

    // The queue holds 64 notes; the 65th is refused and says so; one block empties it.
    for(int i = 0; i < 64; ++i)
        CHECK(SendNote(processor, 40 + i) == kResultOk);
    CHECK(SendNote(processor, 110) == kResultFalse);
    host.Process(64);
    CHECK(SendNote(processor, 110) == kResultOk);
    // All 65 accepted notes reach the engine, four per quantum.
    host.Run(4800);
    CHECK(processor.Engine().TriggerCount() == 1u + 64u + 1u);
    CHECK(processor.Engine().PendingNotes() == 0 && processor.Adapter().PendingEvents() == 0);

    // Messages that are not keyboard notes, or carry nonsense, are refused.
    CHECK(SendNote(processor, 128) != kResultOk && SendNote(processor, -1) != kResultOk);
    CHECK(SendNote(processor, 60, 0) != kResultOk && SendNote(processor, 60, 128) != kResultOk);
    CHECK(SendNote(processor, 60, 100, "SomethingElse") != kResultOk);
    CHECK(processor.notify(nullptr) != kResultOk);
    host.Run(480);
    CHECK(processor.Engine().TriggerCount() == 66u);

    // Keys struck while the audio is off are not played later.
    CHECK(processor.setActive(false) == kResultOk);
    CHECK(SendNote(processor, 70) == kResultOk);
    CHECK(processor.setActive(true) == kResultOk);
    host.Clear();
    host.Run(4800);
    CHECK(Energy(host.Stereo()) == 0.0 && processor.Engine().TriggerCount() == 0);

    // Keys struck while the host does not call process() at all are dropped
    // once they are older than kKeyboardNoteLifetime.
    for(int i = 0; i < 64; ++i)
        CHECK(SendNote(processor, 40 + i) == kResultOk);
    CHECK(SendNote(processor, 110) == kResultFalse); // full, with nobody reading
    std::this_thread::sleep_for(Processor::kKeyboardNoteLifetime + std::chrono::milliseconds(150));
    host.Run(4800);
    CHECK(Energy(host.Stereo()) == 0.0 && processor.Engine().TriggerCount() == 0);
    CHECK(SendNote(processor, 60) == kResultOk); // and the queue is free again
    host.Run(4800);
    CHECK(processor.Engine().TriggerCount() == 1);

    // A parameter flush, process() without samples, leaves keyboard notes
    // where they are: taken there they would wait in the adapter, past their
    // lifetime, and sound whenever audio resumes.
    CHECK(SendNote(processor, 62) == kResultOk);
    host.Process(0);
    CHECK(processor.Adapter().PendingEvents() == 0);
    host.Run(480);
    CHECK(processor.Engine().TriggerCount() == 2); // audio came in time: played
    CHECK(SendNote(processor, 65) == kResultOk);
    host.Process(0);
    std::this_thread::sleep_for(Processor::kKeyboardNoteLifetime + std::chrono::milliseconds(150));
    host.Process(0);
    host.Run(4800);
    CHECK(processor.Engine().TriggerCount() == 2); // audio came too late: dropped
    CHECK(host.allocations == 0);
    std::printf("screen keyboard: notes reach the engine like MIDI at the block start; 64 wait "
                "at most, a full queue reports it; none survive deactivation or a pause of "
                "more than %lld ms\n",
                static_cast<long long>(Processor::kKeyboardNoteLifetime.count()));
}

void TestPauseAndFlush() {
    const std::int64_t total = 48000;
    const HostScore score = Performance(48000.0, total, 9);
    PluginHost straight({48000.0, 2048, false, kRealtime}, score);
    straight.Run(total, 256);

    // A host that stops calling process() for a while and then goes on: time
    // is counted in samples, so nothing changes.
    PluginHost paused({48000.0, 2048, false, kRealtime}, score);
    paused.Run(total / 2, 256);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    paused.Run(total / 2, 256);
    CHECK(paused.Stereo() == straight.Stereo());

    // Parameter flushes: process() with no samples. The values count at once
    // for getState(), and for the engine from the next audio.
    PluginHost flushed({48000.0, 2048, false, kRealtime});
    Processor& processor = *flushed.processor;
    HostScore changes;
    for(int i = 0; i < 20000; ++i) {
        changes.push_back(Param(0, kParamDamping, (i % 100) / 100.0));
        changes.push_back(Param(0, kParamModel, (i % 6) / 5.0));
        changes.push_back(Param(0, kParamPitchBend, (i % 50) / 49.0));
    }
    changes.push_back(Param(0, kParamTune, 0.75));
    changes.push_back(Param(0, kParamVolume, 0.25));
    for(const Ev& change : changes) { // one flush per value
        flushed.AddScore({change});
        flushed.Process(0);
    }
    CHECK(processor.Adapter().PendingEvents() == 5); // the latest of each, nothing piled up
    CHECK(processor.Adapter().DroppedControlPoints() == 0);
    CHECK(processor.Engine().ControlValue(Control::Tune) == 0.5f); // not yet applied
    const auto saved = SavedState(processor);
    double tune = 0.0;
    std::memcpy(&tune, saved.data() + 4, sizeof(tune));
    CHECK(tune == 0.75);
    flushed.Run(4800);
    CHECK(processor.Engine().ControlValue(Control::Tune) == 0.75f);
    CHECK(processor.Engine().ControlValue(Control::Volume) == 0.25f);
    CHECK(processor.Engine().ControlValue(Control::Damping) == 0.99f);
    CHECK(processor.Engine().Model() == 19999 % 6);
    CHECK(processor.Adapter().PendingEvents() == 0);
    // Values outside 0..1 and not-a-number are clamped.
    flushed.AddScore({Param(flushed.position, kParamDamping, std::nan("")),
                      Param(flushed.position, kParamPosition, 5.0),
                      Param(flushed.position, kParamStructure, -3.0),
                      Param(flushed.position, 9999, 0.5)});
    flushed.Run(4800);
    CHECK(processor.Engine().ControlValue(Control::Damping) == 0.0f);
    CHECK(processor.Engine().ControlValue(Control::Position) == 1.0f);
    CHECK(processor.Engine().ControlValue(Control::Structure) == 0.0f);
    CHECK(flushed.allocations == 0);

    // A block with no place to write still lets time pass.
    {
        IPtr<Processor> bare = owned(new Processor());
        CHECK(bare->initialize(nullptr) == kResultOk);
        ProcessSetup setup{kRealtime, kSample32, 64, 48000.0};
        CHECK(bare->setupProcessing(setup) == kResultOk);
        CHECK(bare->setActive(true) == kResultOk);
        ProcessData data{};
        data.symbolicSampleSize = kSample32;
        data.numSamples = 64;
        CHECK(bare->process(data) == kResultOk);
        CHECK(bare->Adapter().HostPosition() == 64);
        data.numSamples = -5;
        CHECK(bare->process(data) == kResultOk);
        CHECK(bare->Adapter().HostPosition() == 64);
        bare->setActive(false);
        bare->terminate();
    }
    std::printf("pause and flush: a pause changes nothing; flushed parameters count at once "
                "for the state, at the next audio for the sound, without piling up\n");
}

void TestActivationAndRateChange() {
    // What a fresh instance plays with these settings.
    const HostScore settings = {Param(0, kParamModel, ModelNormalized(4)),
                                Param(0, kParamDamping, 0.8), Param(0, kParamTune, 0.6)};
    const auto notes = [](double rate, std::int64_t at) {
        HostScore score;
        for(int pitch : {50, 57, 62})
            score.push_back(NoteOn(at + static_cast<std::int64_t>(0.01 * rate), pitch, 100));
        return score;
    };
    for(const double rate : kRates) {
        // A used instance: other rate, sounding notes, events still waiting.
        const double other = rate == 96000.0 ? 44100.0 : 96000.0;
        PluginHost used({other, 2048, false, kRealtime}, settings);
        used.AddScore(notes(other, 0));
        used.Run(static_cast<std::int64_t>(other / 2), 256);
        CHECK(Energy(used.Stereo()) > 0.01);
        CHECK(SendNote(*used.processor, 72) == kResultOk);
        // Deactivate, new rate, activate: the documented reset.
        CHECK(used.processor->setActive(false) == kResultOk);
        used.Setup(rate);
        CHECK(used.processor->setActive(true) == kResultOk);
        CHECK(used.processor->Adapter().HostPosition() == 0);
        CHECK(used.processor->Adapter().PendingEvents() == 0);
        CHECK(used.processor->Engine().TriggerCount() == 0);
        used.Clear();
        const std::int64_t silence = static_cast<std::int64_t>(rate / 4);
        used.Run(silence, 256);
        CHECK(Energy(used.Stereo()) == 0.0); // nothing of the old notes
        used.Clear();
        used.AddScore(notes(rate, used.position));
        used.Run(static_cast<std::int64_t>(rate / 2), 256);

        // The fresh instance gets its settings as a state, not as changes, so
        // that both start from the same values without a glide.
        PluginHost fresh({rate, 2048, false, kRealtime});
        CHECK(fresh.processor->setActive(false) == kResultOk);
        CHECK(LoadState(*fresh.processor, SavedState(*used.processor)) == kResultOk);
        CHECK(fresh.processor->setActive(true) == kResultOk);
        fresh.Run(silence, 256);
        fresh.Clear();
        fresh.AddScore(notes(rate, fresh.position));
        fresh.Run(static_cast<std::int64_t>(rate / 2), 256);
        CHECK(Energy(fresh.Stereo()) > 0.01);
        CHECK(used.Stereo() == fresh.Stereo());
        CHECK(used.processor->getLatencySamples() == fresh.processor->getLatencySamples());
    }
    std::printf("activation and rate change: silence, time zero, no waiting notes; then the "
                "same samples as a fresh instance, at all five rates\n");
}

void TestState() {
    PluginHost host({48000.0, 2048, false, kRealtime});
    Processor& processor = *host.processor;
    const std::vector<double> defaults(kDefaults, kDefaults + kSavedParamCount);
    CHECK(SavedState(processor) == MakeState(kStateVersion, defaults));
    CHECK(SavedState(processor).size() == 4 + 8 * 8);

    // A valid state is taken over; the model lands on its step.
    const std::vector<double> values = {0.1, 0.2, 0.3, 0.4, 0.55, 0.6, 0.5, 0.7};
    CHECK(LoadState(processor, MakeState(kStateVersion, values)) == kResultOk);
    std::vector<double> stored = values;
    stored[6] = ModelNormalized(ModelIndex(0.5)); // 0.5 falls on model 3 = 0.6
    CHECK(SavedState(processor) == MakeState(kStateVersion, stored));
    host.Run(480);
    CHECK(processor.Engine().Model() == 3);
    CHECK(processor.Engine().ControlValue(Control::Tune) == 0.1f);
    CHECK(processor.Engine().ControlValue(Control::Volume) == 0.7f);

    // Damaged states change nothing: wrong version, cut short at every
    // length, values that are not finite or outside 0..1.
    const auto good = SavedState(processor);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<std::vector<char>> damaged = {MakeState(0, values), MakeState(2, values),
                                              MakeState(-1, values), {}};
    for(std::size_t length = 1; length < good.size(); ++length)
        damaged.emplace_back(good.begin(), good.begin() + static_cast<std::ptrdiff_t>(length));
    for(int index = 0; index < kSavedParamCount; ++index)
        for(const double bad : {nan, inf, -inf, -0.001, 1.001, 1e300, -1e300}) {
            std::vector<double> broken = values;
            broken[index] = bad;
            damaged.push_back(MakeState(kStateVersion, broken));
        }
    // The sound must not notice a refused state either: compare with an
    // instance that never saw one.
    PluginHost undisturbed({48000.0, 2048, false, kRealtime});
    CHECK(LoadState(*undisturbed.processor, good) == kResultOk);
    undisturbed.Run(480);
    const HostScore notes = {NoteOn(host.position + 10, 60, 100), NoteOn(host.position + 10, 67, 90)};
    host.AddScore(notes);
    undisturbed.AddScore(notes);
    host.Clear();
    undisturbed.Clear();
    for(const auto& bytes : damaged) {
        CHECK(LoadState(processor, bytes) != kResultOk);
        CHECK(SavedState(processor) == good);
        host.Process(64);
        undisturbed.Process(64);
    }
    CHECK(processor.setState(nullptr) != kResultOk && processor.getState(nullptr) != kResultOk);
    CHECK(host.Stereo() == undisturbed.Stereo() && Energy(host.Stereo()) > 0.01);
    std::printf("  %zu damaged states refused without a trace\n", damaged.size());

    // Loading while notes ring: the sound restarts from silence with the new
    // values at the next block, the pitch wheel centred, waiting events gone.
    host.AddScore({Param(host.position, kParamPitchBend, 1.0),
                   NoteOn(host.position + 63, 76, 100)}); // belongs to a quantum not yet rendered
    host.Process(64);
    CHECK(processor.Adapter().PendingEvents() == 1);
    CHECK(SendNote(processor, 79) == kResultOk); // a screen key struck before the load
    const std::vector<double> next = {0.5, 0.9, 0.1, 0.6, 0.2, 1.0, 1.0, 0.5};
    CHECK(LoadState(processor, MakeState(kStateVersion, next)) == kResultOk);
    CHECK(SavedState(processor) == MakeState(kStateVersion, next)); // at once
    CHECK(SendNote(processor, 72) == kResultOk); // and one struck after it
    host.Clear();
    host.Run(4800);
    // The MIDI note that was waiting and the screen key struck before the load
    // are gone with the old sound; the key struck after it plays the new one.
    CHECK(processor.Engine().TriggerCount() == 1 && processor.Engine().VoiceNote(0) == 72.0f);
    CHECK(processor.Engine().Model() == 5 && processor.Engine().PitchOffset() == 0.0f);
    PluginHost fresh({48000.0, 2048, false, kRealtime});
    CHECK(fresh.processor->setActive(false) == kResultOk);
    CHECK(LoadState(*fresh.processor, MakeState(kStateVersion, next)) == kResultOk);
    CHECK(fresh.processor->setActive(true) == kResultOk);
    fresh.AddScore({NoteOn(0, 72, kKeyboardVelocity)});
    fresh.Run(4800);
    CHECK(host.Stereo() == fresh.Stereo() && Energy(fresh.Stereo()) > 0.001);
    CHECK(host.allocations == 0);

    // A state loaded while the plug-in is inactive, after automation: the
    // restart at activation begins from the loaded state.
    {
        PluginHost inactive({48000.0, 2048, false, kRealtime});
        inactive.AddScore({Param(0, kParamDamping, 0.7), Param(0, kParamPitchBend, 0.9)});
        inactive.Run(4800); // long enough for the 5 ms smoothing to land on the value
        CHECK(inactive.processor->Engine().ControlValue(Control::Damping) == 0.7f);
        CHECK(inactive.processor->setActive(false) == kResultOk);
        CHECK(LoadState(*inactive.processor, MakeState(kStateVersion, next)) == kResultOk);
        CHECK(inactive.processor->setActive(true) == kResultOk);
        CHECK(inactive.processor->Engine().ControlValue(Control::Damping) == 0.6f);
        CHECK(inactive.processor->Engine().PitchOffset() == 0.0f);
        CHECK(SavedState(*inactive.processor) == MakeState(kStateVersion, next));
    }

    // Two complete states loaded in turn from a second thread while audio
    // runs in blocks of one sample. After every block the engine holds one of
    // them whole, never a mixture (review finding TC-R01), and getState()
    // returns the state that was just loaded, whatever the audio thread does.
    {
        PluginHost live({48000.0, 2048, false, kRealtime});
        const std::vector<double> low(kSavedParamCount, 0.1), high(kSavedParamCount, 0.9);
        std::atomic<bool> stop{false};
        std::atomic<int> loads{0};
        std::thread loader([&] {
            bool flip = false;
            while(!stop.load()) {
                std::vector<double> state = flip ? high : low;
                flip = !flip;
                CHECK(LoadState(*live.processor, MakeState(kStateVersion, state)) == kResultOk);
                state[kParamModel - kFirstParam] = ModelNormalized(ModelIndex(state[6]));
                CHECK(SavedState(*live.processor) == MakeState(kStateVersion, state));
                loads.fetch_add(1);
            }
        });
        const DryEngine& engine = live.processor->Engine();
        const auto all = [&](float value, float excitation, float volume) {
            return engine.ControlValue(Control::Tune) == value
                   && engine.ControlValue(Control::Structure) == value
                   && engine.ControlValue(Control::Brightness) == value
                   && engine.ControlValue(Control::Damping) == value
                   && engine.ControlValue(Control::Position) == value
                   && engine.ControlValue(Control::Excitation) == excitation
                   && engine.ControlValue(Control::Volume) == volume;
        };
        int low_blocks = 0, high_blocks = 0, switches = 0, last = -1;
        for(int block = 0; block < 2000000 && (switches < 2000 || block < 100000); ++block) {
            live.Process(1);
            const bool is_low = all(0.1f, 0.1f, 0.1f), is_high = all(0.9f, 0.9f, 0.9f);
            // Before the first state arrives: the defaults, also complete.
            CHECK(is_low || is_high || all(0.5f, 0.8f, 1.0f));
            CHECK(!is_low || engine.Model() == 0);
            CHECK(!is_high || engine.Model() == 5);
            low_blocks += is_low;
            high_blocks += is_high;
            const int now = is_low ? 0 : (is_high ? 1 : -1);
            switches += now != last;
            last = now;
            if(block % 4096 == 0) {
                live.Clear();
                std::this_thread::yield();
            }
        }
        stop.store(true);
        loader.join();
        CHECK(low_blocks > 0 && high_blocks > 0 && live.allocations == 0);
        std::printf("  %d states loaded in turn during %d one-sample blocks: %d blocks on the "
                    "one, %d on the other, %d changes seen, never a mixture\n",
                    loads.load(), low_blocks + high_blocks, low_blocks, high_blocks, switches);
    }

    // Random and damaged states from another thread while a performance
    // plays: nothing fails, and the last state wins.
    {
        // Two seconds of notes and automation, then audio without events, so
        // that the last check sees the loaded values untouched.
        PluginHost live({48000.0, 2048, false, kRealtime}, Performance(48000.0, 96000, 31));
        std::atomic<bool> stop{false};
        std::atomic<int> loads{0};
        std::thread loader([&] {
            Lcg random(8);
            while(!stop.load()) {
                std::vector<double> state(kSavedParamCount);
                for(double& v : state)
                    v = random.Unit();
                CHECK(LoadState(*live.processor, MakeState(kStateVersion, state)) == kResultOk);
                if(random.Below(3) == 0) // and damaged ones in between
                    CHECK(LoadState(*live.processor, MakeState(kStateVersion + 1, state))
                          != kResultOk);
                loads.fetch_add(1);
                std::this_thread::yield();
            }
        });
        while((loads.load() < 2000 || live.position < 144000) && live.position < 480000)
            live.Process(64);
        stop.store(true);
        loader.join();
        CHECK(loads.load() > 0 && live.allocations == 0);
        const std::vector<double> final_state = {0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.2, 0.9};
        CHECK(LoadState(*live.processor, MakeState(kStateVersion, final_state)) == kResultOk);
        live.Process(64);
        CHECK(live.processor->Engine().ControlValue(Control::Structure) == 0.4f);
        CHECK(live.processor->Engine().Model() == 1);
        std::printf("  %d states loaded from a second thread during %lld samples of audio\n",
                    loads.load(), static_cast<long long>(live.position));
    }
    std::printf("state: version and eight values; damaged states refused whole; loading "
                "restarts the sound with the new values and a centred pitch wheel\n");
}

// --- The controller -----------------------------------------------------------

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

    // The parameters as the host sees them.
    const struct {
        ParamID id;
        const char* title;
        int32 steps;
        double default_value;
    } expected[] = {{100, "Tune", 0, 0.5},       {101, "Structure", 0, 0.5},
                    {102, "Brightness", 0, 0.5}, {103, "Damping", 0, 0.5},
                    {104, "Position", 0, 0.5},   {105, "Excitation", 0, 0.8},
                    {106, "Model", 5, 0.0},      {107, "Volume", 0, 1.0},
                    {108, "Pitch Bend", 0, 0.5}};
    for(int i = 0; i < kParamCount; ++i) {
        ParameterInfo info{};
        CHECK(controller->getParameterInfo(i, info) == kResultOk);
        CHECK(info.id == expected[i].id && info.stepCount == expected[i].steps);
        CHECK(StringConvert::convert(info.title) == expected[i].title);
        CHECK(info.defaultNormalizedValue == expected[i].default_value);
        CHECK(info.flags & ParameterInfo::kCanAutomate);
        CHECK(((info.flags & ParameterInfo::kIsList) != 0) == (info.id == kParamModel));
        CHECK(controller->getParamNormalized(info.id) == expected[i].default_value);
    }

    // Tune is continuous, in semitones.
    CHECK(Text(*controller, kParamTune, 0.5) == "0.00" && Text(*controller, kParamTune, 0.0) == "-24.00");
    CHECK(Text(*controller, kParamTune, 1.0) == "24.00");
    CHECK(Text(*controller, kParamTune, 0.5 + 0.37 / 48.0) == "0.37");
    CHECK(Text(*controller, kParamTune, 0.5 - 0.001 / 48.0) == "0.00"); // no "-0.00"
    CHECK(controller->normalizedParamToPlain(kParamTune, 0.75) == 12.0);
    CHECK(controller->plainParamToNormalized(kParamTune, -12.0) == 0.25);
    CHECK(Text(*controller, kParamStructure, 0.5) == "0.500" && Text(*controller, kParamDamping, 1.0) == "1.000");
    CHECK(Text(*controller, kParamVolume, 1.0) == "100.0" && Text(*controller, kParamVolume, 0.0) == "0.0");
    CHECK(Text(*controller, kParamVolume, 0.255) == "25.5");
    CHECK(Text(*controller, kParamPitchBend, 0.5) == "0.00" && Text(*controller, kParamPitchBend, 0.0) == "-2.00");
    CHECK(Text(*controller, kParamPitchBend, 1.0) == "2.00");
    // A MIDI pitch wheel in its centre (8192 of 16383) is exactly no bend.
    CHECK(PitchBendSemitones(8192.0 / 16383.0) == 0.0 && PitchBendSemitones(0.5) == 0.0);
    CHECK(PitchBendSemitones(0.0) == -2.0 && PitchBendSemitones(1.0) == 2.0);

    // The model: display, typed text, normalized value and engine agree on
    // the VST3 step rule, for every value a host or a MIDI controller can send.
    for(int model = 0; model < kModelCount; ++model) {
        CHECK(Text(*controller, kParamModel, ModelNormalized(model)) == kModelNames[model]);
        ParamValue normalized = -1.0;
        CHECK(Typed(*controller, kParamModel, kModelNames[model], normalized)
              && normalized == ModelNormalized(model));
        std::string shouted = kModelNames[model];
        for(char& c : shouted)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        CHECK(Typed(*controller, kParamModel, ("  " + shouted + " ").c_str(), normalized)
              && normalized == ModelNormalized(model));
        CHECK(Typed(*controller, kParamModel, std::to_string(model + 1).c_str(), normalized)
              && normalized == ModelNormalized(model));
        CHECK(controller->normalizedParamToPlain(kParamModel, ModelNormalized(model)) == model);
        CHECK(controller->plainParamToNormalized(kParamModel, model) == ModelNormalized(model));
    }
    {
        PluginHost host({48000.0, 2048, false, kRealtime});
        std::vector<double> candidates;
        for(int cc = 0; cc < 128; ++cc)
            candidates.push_back(cc / 127.0);
        for(int i = 0; i <= 600; ++i)
            candidates.push_back(i / 600.0);
        for(int step = 1; step <= 5; ++step) // just below and on every boundary
            for(const double delta : {-1e-12, 0.0, 1e-12})
                candidates.push_back(step / 6.0 + delta);
        for(const double value : candidates) {
            const int model = ModelIndex(value);
            CHECK(model == std::min(5, static_cast<int>(value * 6)));
            CHECK(Text(*controller, kParamModel, value) == kModelNames[model]);
            host.AddScore({Param(host.position, kParamModel, value)});
            host.Process(16);
            CHECK(host.processor->Engine().DesiredModel() == static_cast<unsigned>(model));
        }
    }

    // Typed values: strict. One finite number, optionally the unit, nothing
    // else; on refusal the output value stays as it was.
    double value = 7.0;
    for(const auto& [text, unit, parsed] :
        {std::tuple{"50", "%", 50.0}, std::tuple{" 50 % ", "%", 50.0},
         std::tuple{"+2.5", "", 2.5}, std::tuple{"2,5", "", 2.5}, std::tuple{"-7ST", "st", -7.0},
         std::tuple{"1e2", "", 100.0}, std::tuple{"-0.5", "st", -0.5}})
        CHECK(ParseTypedNumber(text, unit, value) && value == parsed);
    const char* const junk[] = {"",     "   ",  "foo",  "12junk",   "nan",      "NaN",  "inf",
                                "+inf", "-inf", "1,2,3", "st",      "5 s",      "--5",  "+",
                                "+-5",  "++5",  "-+5",  "+-5 st",   "infinity", "1e999", "0x10",
                                "5 5",  "1.2.3", "%",   "1e",       "-",        "."};
    for(const char* text : junk) {
        value = 7.0;
        CHECK(!ParseTypedNumber(text, "st", value));
        CHECK(value == 7.0);
        for(int i = 0; i < kParamCount; ++i) {
            ParamValue normalized = 0.123;
            CHECK(!Typed(*controller, kFirstParam + static_cast<ParamID>(i), text, normalized));
            CHECK(normalized == 0.123);
        }
    }
    ParamValue normalized = 0.0;
    CHECK(Typed(*controller, kParamTune, "-7.5 st", normalized)
          && std::fabs(TuneSemitones(normalized) + 7.5) < 1e-12);
    CHECK(Typed(*controller, kParamTune, "+30", normalized) && normalized == 1.0); // clamped
    CHECK(Typed(*controller, kParamTune, "-99 st", normalized) && normalized == 0.0);
    CHECK(Typed(*controller, kParamTune, "0,37", normalized)
          && std::fabs(TuneSemitones(normalized) - 0.37) < 1e-12); // not snapped to semitones
    CHECK(Typed(*controller, kParamStructure, "0.25", normalized) && normalized == 0.25);
    CHECK(Typed(*controller, kParamStructure, "3", normalized) && normalized == 1.0);
    CHECK(Typed(*controller, kParamVolume, "50 %", normalized) && normalized == 0.5);
    CHECK(Typed(*controller, kParamVolume, "50", normalized) && normalized == 0.5);
    CHECK(Typed(*controller, kParamVolume, "250", normalized) && normalized == 1.0);
    CHECK(Typed(*controller, kParamPitchBend, "1", normalized)
          && std::fabs(PitchBendSemitones(normalized) - 1.0) < 1e-3);
    CHECK(Typed(*controller, kParamPitchBend, "-2 st", normalized) && normalized == 0.0);
    for(const char* text : {"0", "7", "2.5", "Modal", "Stereo Strings 2", "-1"}) {
        normalized = 0.123;
        CHECK(!Typed(*controller, kParamModel, text, normalized) && normalized == 0.123);
    }

    // MIDI: the firmware's controller numbers, on all 16 channels of bus 0.
    IMidiMapping* mapping = nullptr;
    CHECK(controller->queryInterface(IMidiMapping::iid, reinterpret_cast<void**>(&mapping))
          == kResultOk);
    const std::pair<int, ParamID> map[] = {
        {kPitchBend, kParamPitchBend}, {20, kParamTune},     {21, kParamStructure},
        {22, kParamBrightness},        {23, kParamDamping},  {24, kParamPosition},
        {25, kParamExcitation},        {28, kParamModel},    {33, kParamVolume}};
    for(int16 channel = 0; channel < 16; ++channel) {
        for(const auto& [number, param] : map) {
            ParamID id = 0;
            CHECK(mapping->getMidiControllerAssignment(0, channel,
                                                       static_cast<CtrlNumber>(number), id)
                  == kResultTrue);
            CHECK(id == param);
        }
        int mapped = 0;
        for(int number = 0; number < kCountCtrlNumber; ++number) {
            ParamID id = 0;
            mapped += mapping->getMidiControllerAssignment(0, channel,
                                                           static_cast<CtrlNumber>(number), id)
                      == kResultTrue;
        }
        CHECK(mapped == 9); // none of the firmware's reverb or polyphony controllers
        ParamID id = 0;
        for(const int unused : {26, 27, 29, 30, 31, 32, 1, 7, 64})
            CHECK(mapping->getMidiControllerAssignment(0, channel,
                                                       static_cast<CtrlNumber>(unused), id)
                  != kResultTrue);
        CHECK(mapping->getMidiControllerAssignment(1, channel, 20, id) != kResultTrue);
    }
    ParamID id = 0;
    CHECK(mapping->getMidiControllerAssignment(0, 16, 20, id) != kResultTrue);
    CHECK(mapping->getMidiControllerAssignment(0, -1, 20, id) != kResultTrue);
    mapping->release();

    // The controller takes over what the processor saved; the pitch wheel
    // returns to its centre on both sides.
    {
        PluginHost host({48000.0, 2048, false, kRealtime});
        const std::vector<double> values = {0.1, 0.2, 0.3, 0.4, 0.55, 0.6, 0.8, 0.7};
        CHECK(LoadState(*host.processor, MakeState(kStateVersion, values)) == kResultOk);
        CHECK(controller->setParamNormalized(kParamPitchBend, 0.9) == kResultOk);
        const auto load = [&](const std::vector<char>& bytes) {
            MemoryStream stream;
            int32 written = 0;
            if(!bytes.empty())
                stream.write(const_cast<char*>(bytes.data()), static_cast<int32>(bytes.size()),
                             &written);
            int64 start = 0;
            stream.seek(0, IBStream::kIBSeekSet, &start);
            return controller->setComponentState(&stream);
        };
        const auto saved = SavedState(*host.processor);
        CHECK(load(saved) == kResultOk);
        for(int i = 0; i < kSavedParamCount; ++i)
            CHECK(controller->getParamNormalized(kFirstParam + static_cast<ParamID>(i))
                  == values[i]);
        CHECK(controller->getParamNormalized(kParamPitchBend) == kDefaultPitchBend);
        // Damaged: nothing changes, the pitch wheel included.
        CHECK(controller->setParamNormalized(kParamPitchBend, 0.9) == kResultOk);
        std::vector<double> broken = values;
        broken[3] = std::numeric_limits<double>::quiet_NaN();
        for(const auto& bytes :
            {MakeState(kStateVersion, broken), MakeState(kStateVersion + 1, values),
             std::vector<char>(saved.begin(), saved.end() - 1), std::vector<char>{}}) {
            CHECK(load(bytes) != kResultOk);
            for(int i = 0; i < kSavedParamCount; ++i)
                CHECK(controller->getParamNormalized(kFirstParam + static_cast<ParamID>(i))
                      == values[i]);
            CHECK(controller->getParamNormalized(kParamPitchBend) == 0.9);
        }
        CHECK(controller->setComponentState(nullptr) != kResultOk);
    }

    // The controller's own state: the window's zoom and whether the keyboard shows.
    const auto window_state = [](int32 version, double zoom, int keyboard) {
        auto stream = owned(new MemoryStream());
        int32 written = 0;
        stream->write(&version, sizeof(version), &written);
        stream->write(&zoom, sizeof(zoom), &written);
        if(keyboard >= 0) {
            int8 flag = static_cast<int8>(keyboard);
            stream->write(&flag, sizeof(flag), &written);
        }
        int64 start = 0;
        stream->seek(0, IBStream::kIBSeekSet, &start);
        return stream;
    };
    const auto is = [&](double zoom, bool keyboard) {
        MemoryStream state;
        CHECK(controller->getState(&state) == kResultOk);
        CHECK(state.getSize() == 4 + 8 + 1);
        int32 version = 0;
        double saved_zoom = 0.0;
        std::memcpy(&version, state.getData(), sizeof(version));
        std::memcpy(&saved_zoom, state.getData() + 4, sizeof(saved_zoom));
        return version == kControllerStateVersion && saved_zoom == zoom
               && (state.getData()[12] != 0) == keyboard;
    };
    CHECK(is(1.0, true)); // keyboard shown by default
    CHECK(controller->setState(window_state(1, 1.5, 0)) == kResultOk);
    CHECK(is(1.5, false));
    CHECK(controller->setState(window_state(1, 10.0, 1)) == kResultOk);
    CHECK(is(kMaxEditorZoom, true)); // finite but out of range: pulled in
    CHECK(controller->setState(window_state(1, 0.01, 0)) == kResultOk);
    CHECK(is(kMinEditorZoom, false));
    for(const auto& bad :
        {window_state(2, 1.5, 1), window_state(0, 1.5, 1), window_state(1, 1.5, -1),
         window_state(1, std::numeric_limits<double>::quiet_NaN(), 1),
         window_state(1, std::numeric_limits<double>::infinity(), 1), window_state(1, 1.5, 2),
         window_state(1, 1.5, 255)}) {
        CHECK(controller->setState(bad) != kResultOk);
        CHECK(is(kMinEditorZoom, false)); // a rejected state changes nothing
    }
    MemoryStream empty;
    CHECK(controller->setState(&empty) != kResultOk && controller->setState(nullptr) != kResultOk);
    // No host messages available: a screen key plays nothing and nothing breaks.
    CHECK(!controller->PlayNote(60));
    CHECK(!controller->PlayNote(-1) && !controller->PlayNote(128));
    controller->terminate();
    std::printf("controller: nine parameters, value texts, strict typed values (\"+-5\" "
                "refused), model step rule shared with the engine, MIDI map, states\n");
}

// --- Window geometry ----------------------------------------------------------

void TestViewLayout() {
    constexpr double width = 620.0, height = 68.0;
    int white = 0, black = 0;
    for(int note = kKeyboardLowNote; note <= kKeyboardHighNote; ++note) {
        const KeyRect key = KeyRectFor(note, width, height);
        // 620 / 15 is no whole number: allow for the rounding of the last key's edge.
        CHECK(key.left >= 0.0 && key.right <= width + 1e-9 && key.bottom <= height);
        const double centre = (key.left + key.right) / 2.0;
        if(IsBlackKey(note)) {
            ++black;
            CHECK(NoteAt(centre, height * 0.3, width, height) == note);
            const double boundary = (WhiteKeyIndex(note) + 1) * width / kKeyboardWhiteKeys;
            CHECK(NoteAt(boundary - 1.0, height * 0.9, width, height) == note - 1);
            CHECK(NoteAt(boundary + 1.0, height * 0.9, width, height) == note + 1);
        } else {
            ++white;
            CHECK(NoteAt(centre, height * 0.9, width, height) == note);
        }
    }
    CHECK(white == kKeyboardWhiteKeys && black == 10);
    CHECK(kKeyboardLowNote == 60 && kKeyboardHighNote == 84 && kKeyboardVelocity == 100);
    for(const auto& [x, y] : {std::pair{-1.0, 10.0}, {width, 10.0}, {10.0, height}, {10.0, -1.0}})
        CHECK(NoteAt(x, y, width, height) == -1);

    // The model selector: six cells, each found at its centre, gaps between.
    constexpr double selector_width = 620.0, selector_height = 56.0;
    for(int model = 0; model < kModelCount; ++model) {
        const KeyRect cell = ModelRectFor(model, selector_width, selector_height);
        CHECK(cell.left >= 0.0 && cell.right <= selector_width + 1e-9);
        CHECK(cell.top >= 0.0 && cell.bottom <= selector_height + 1e-9);
        CHECK(ModelAt((cell.left + cell.right) / 2, (cell.top + cell.bottom) / 2, selector_width,
                      selector_height)
              == model);
        CHECK(ModelAt(cell.left + 0.5, cell.top + 0.5, selector_width, selector_height) == model);
    }
    const KeyRect first = ModelRectFor(0, selector_width, selector_height);
    CHECK(ModelAt(first.right + kModelGap / 2, 5.0, selector_width, selector_height) == -1);
    CHECK(ModelAt(5.0, first.bottom + kModelGap / 2, selector_width, selector_height) == -1);
    CHECK(ModelAt(-1.0, 5.0, selector_width, selector_height) == -1);
    std::printf("layout: 25 keys C4..C6 and six model cells, each where it is drawn\n");
}

void TestEditorResize() {
    using Clock = DragConstraint::Clock;
    const auto same = [](EditorSize a, EditorSize b) {
        return a.width == b.width && a.height == b.height;
    };
    Clock::time_point now{};
    const auto request = [&](DragConstraint& drag, EditorSize asked, EditorSize current,
                             double scale, bool keyboard) {
        now += std::chrono::milliseconds(10);
        return drag.Apply(asked, current, scale, keyboard, now);
    };
    const EditorSize base = EditorSizeFor(1.0, false);
    const EditorSize with_keys = EditorSizeFor(1.0, true);
    CHECK(same(base, {660, 250}) && same(with_keys, {660, 336}));
    {
        DragConstraint drag; // an edge stays under the mouse, the other side follows
        CHECK(same(request(drag, {990, 250}, base, 1.0, false), {990, 375}));
        CHECK(same(request(drag, {660, 500}, base, 1.0, false), {1320, 500}));
    }
    // Limits: 50 % and 200 %, also on a scaled display.
    const EditorSize scaled = EditorSizeFor(1.5, false);
    for(const auto& [asked, current, scale, expected] :
        {std::tuple{EditorSize{10, 10}, base, 1.0, EditorSize{330, 125}},
         std::tuple{EditorSize{9000, 250}, base, 1.0, EditorSize{1320, 500}},
         std::tuple{EditorSize{10, 10}, scaled, 1.5, EditorSize{495, 188}},
         std::tuple{EditorSize{9000, 375}, scaled, 1.5, EditorSize{1980, 750}}}) {
        DragConstraint drag;
        CHECK(same(request(drag, asked, current, scale, false), expected));
    }
    // Every granted size has the layout's aspect ratio within a pixel.
    for(const bool keyboard : {false, true})
        for(const bool corner : {false, true})
            for(int width = 200; width <= 1500; width += 7)
                for(int height = 80; height <= 800; height += 31) {
                    const EditorSize size = ConstrainEditorSize(
                        {width, height}, EditorSizeFor(1.0, keyboard), 1.0, keyboard, corner);
                    CHECK(std::abs(size.height
                                   - size.width * EditorBaseHeight(keyboard) / kEditorWidth)
                          < 1.0);
                    CHECK(size.width >= 330 && size.width <= 1320);
                }
    // A corner drag grows steadily, also when the mouse meets the old height.
    for(const bool keyboard : {false, true}) {
        const EditorSize anchor = EditorSizeFor(1.0, keyboard);
        DragConstraint drag;
        EditorSize current = anchor;
        for(int step = 1; step <= 60; ++step) {
            const EditorSize mouse = {anchor.width + 7 * step, anchor.height + 2 * step};
            const EditorSize next = request(drag, mouse, current, 1.0, keyboard);
            CHECK(next.width >= current.width && next.height >= current.height);
            CHECK(next.width - current.width <= 8);
            current = next;
        }
    }
    // A window size leads back to the zoom it was made for.
    for(const bool keyboard : {false, true})
        for(const double scale : {1.0, 1.25, 1.5, 2.0})
            for(const double zoom : {0.5, 0.75, 1.0, 1.3, 2.0}) {
                const EditorSize size = EditorSizeFor(scale * zoom, keyboard);
                CHECK(std::fabs(EditorZoomForSize(size.width, size.height, scale, keyboard) - zoom)
                      < 0.005);
            }
    CHECK(ClampEditorZoom(std::nan("")) == kMinEditorZoom);
    std::printf("editor resize: aspect ratio kept, zoom 50..200 %%\n");
}

// --- The editor in a window, with a host stand-in ------------------------------

// Answers the window's size requests. Accepting, it does what a host does:
// resizes its window, then calls the view's onSize().
class FakeFrame : public IPlugFrame {
  public:
    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* size) override {
        ++requests;
        view->getSize(&size_at_request);
        if(auto* editor = dynamic_cast<Editor*>(view)) {
            const VSTGUI::CRect frame = editor->getFrame()->getViewSize();
            frame_at_request = {static_cast<int>(frame.getWidth()),
                                static_cast<int>(frame.getHeight())};
        }
        if(!accept)
            return kResultFalse;
        view->onSize(size);
        return kResultTrue;
    }
    tresult PLUGIN_API queryInterface(const TUID, void** obj) override {
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    bool accept = true;
    int requests = 0;
    ViewRect size_at_request{};
    EditorSize frame_at_request{};
};

// Records what the controller tells the host.
class FakeHandler : public IComponentHandler, public IComponentHandler2 {
  public:
    struct Edit {
        char kind; // 'b'egin, 'p'erform, 'e'nd
        ParamID id;
        ParamValue value;
    };
    tresult PLUGIN_API beginEdit(ParamID id) override {
        edits.push_back({'b', id, 0.0});
        return kResultOk;
    }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override {
        edits.push_back({'p', id, value});
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID id) override {
        edits.push_back({'e', id, 0.0});
        return kResultOk;
    }
    tresult PLUGIN_API restartComponent(int32) override {
        ++restarts;
        return kResultOk;
    }
    tresult PLUGIN_API setDirty(TBool state) override {
        dirty += state ? 1 : 0;
        return kResultOk;
    }
    tresult PLUGIN_API requestOpenEditor(FIDString) override { return kResultFalse; }
    tresult PLUGIN_API startGroupEdit() override { return kResultOk; }
    tresult PLUGIN_API finishGroupEdit() override { return kResultOk; }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        if(FUnknownPrivate::iidEqual(iid, IComponentHandler::iid)
           || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = static_cast<IComponentHandler*>(this);
            return kResultOk;
        }
        if(FUnknownPrivate::iidEqual(iid, IComponentHandler2::iid)) {
            *obj = static_cast<IComponentHandler2*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    std::vector<Edit> edits;
    int restarts = 0, dirty = 0;
};

void PumpMessages() {
    MSG message;
    while(PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

int ChildWindows(HWND parent) {
    int count = 0;
    for(HWND child = GetWindow(parent, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        ++count;
    return count;
}

void TestEditorInWindow() {
    using VSTGUI::CRect;
    moduleHandle = GetModuleHandleW(nullptr);
    CHECK(InitModule()); // what a plug-in's DLL entry does: sets up VSTGUI
    const auto make_parent = [] {
        HWND window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 2400, 1400, nullptr,
                                      nullptr, GetModuleHandleW(nullptr), nullptr);
        CHECK(window != nullptr);
        return window;
    };
    HWND parent = make_parent(), second_parent = make_parent();

    // Both halves, connected as a host connects them.
    IPtr<HostApplication> application = owned(new HostApplication());
    IPtr<Processor> processor = owned(new Processor());
    IPtr<Controller> controller = owned(new Controller());
    CHECK(processor->initialize(application) == kResultOk);
    CHECK(controller->initialize(application) == kResultOk);
    CHECK(processor->connect(controller) == kResultOk);
    CHECK(controller->connect(processor) == kResultOk);
    FakeHandler handler;
    CHECK(controller->setComponentHandler(&handler) == kResultOk);

    const auto same = [](EditorSize a, EditorSize b) {
        return a.width == b.width && a.height == b.height;
    };
    const auto view_size = [](IPlugView* view) {
        ViewRect size{};
        CHECK(view->getSize(&size) == kResultOk);
        return EditorSize{size.getWidth(), size.getHeight()};
    };
    const auto frame_size = [](Editor* editor) {
        const CRect size = editor->getFrame()->getViewSize();
        return EditorSize{static_cast<int>(size.getWidth()), static_cast<int>(size.getHeight())};
    };
    const auto as_editor = [](IPlugView* view) {
        auto* editor = dynamic_cast<Editor*>(view);
        CHECK(editor != nullptr);
        return editor;
    };
    const auto open_view = [&](FakeFrame& frame, HWND window) {
        IPtr<IPlugView> view = owned(controller->createView(ViewType::kEditor));
        CHECK(view != nullptr);
        CHECK(view->isPlatformTypeSupported(kPlatformTypeHWND) == kResultTrue);
        CHECK(view->setFrame(&frame) == kResultOk);
        CHECK(view->attached(window, kPlatformTypeHWND) == kResultOk);
        return view;
    };
    CHECK(controller->createView("something else") == nullptr);

    // Automation while no window is open reaches the controller only.
    CHECK(controller->setParamNormalized(kParamDamping, 0.9) == kResultOk);
    CHECK(controller->setParamNormalized(kParamModel, ModelNormalized(4)) == kResultOk);
    CHECK(controller->OpenEditorCount() == 0);

    // Opening: the window shows what the parameters are.
    FakeFrame host;
    IPtr<IPlugView> view = open_view(host, parent);
    Editor* editor = as_editor(view);
    CHECK(controller->OpenEditorCount() == 1);
    CHECK(ChildWindows(parent) == 1);
    CHECK(editor->KeyboardVisible());
    CHECK(same(view_size(view), {660, 336}) && same(frame_size(editor), {660, 336}));
    CHECK(editor->ShownModel() == 4 && editor->ShownKnobValue(kParamDamping) == 0.9f);
    CHECK(editor->ShownKnobValue(kParamVolume) == 1.0f);
    CHECK(view->canResize() == kResultTrue);

    // Automation while it is open moves knob and selector.
    CHECK(controller->setParamNormalized(kParamDamping, 0.25) == kResultOk);
    CHECK(controller->setParamNormalized(kParamModel, 0.17) == kResultOk); // step 1
    CHECK(editor->ShownKnobValue(kParamDamping) == 0.25f && editor->ShownModel() == 1);
    CHECK(handler.edits.empty()); // none of this was an edit by the user

    // A click on the selector is one: begin, perform, end.
    editor->ChooseModel(5);
    CHECK(handler.edits.size() == 3 && handler.edits[0].kind == 'b'
          && handler.edits[1].kind == 'p' && handler.edits[2].kind == 'e');
    CHECK(handler.edits[1].id == kParamModel && handler.edits[1].value == 1.0);
    CHECK(controller->getParamNormalized(kParamModel) == 1.0 && editor->ShownModel() == 5);
    editor->ChooseModel(99);
    CHECK(handler.edits.size() == 3);
    handler.edits.clear();

    // A knob, moved the way VSTGUI moves it: the host hears begin, perform
    // and end exactly once each (review finding TC-R02), also for nested
    // edits such as a wheel turn during a drag.
    const auto kinds = [&] {
        std::string text;
        for(const auto& edit : handler.edits)
            text += edit.kind;
        return text;
    };
    VSTGUI::CKnob* knob = editor->KnobOf(kParamBrightness);
    CHECK(knob != nullptr && editor->KnobOf(kParamModel) == nullptr);
    knob->beginEdit();
    knob->setValue(0.3f);
    knob->valueChanged();
    knob->endEdit();
    CHECK(kinds() == "bpe");
    for(const auto& edit : handler.edits)
        CHECK(edit.id == kParamBrightness);
    CHECK(handler.edits[1].value == static_cast<double>(0.3f));
    CHECK(controller->getParamNormalized(kParamBrightness) == static_cast<double>(0.3f));
    handler.edits.clear();
    knob->beginEdit();
    knob->beginEdit();
    knob->setValue(0.4f);
    knob->valueChanged();
    knob->endEdit();
    knob->endEdit();
    CHECK(kinds() == "bpe");
    handler.edits.clear();

    // KEYS off, host agrees: the window changes only once the host has agreed.
    int requests = host.requests;
    editor->SetKeyboardVisible(false);
    CHECK(host.requests == requests + 1);
    CHECK(host.size_at_request.getHeight() == 336 && host.frame_at_request.height == 336);
    CHECK(!editor->KeyboardVisible());
    CHECK(same(view_size(view), {660, 250}) && same(frame_size(editor), {660, 250}));
    // The keyboard switch is no musical parameter: the host hears of no edit.
    CHECK(handler.edits.empty());
    CHECK(!controller->WindowSettings().keyboard);

    // KEYS on, host refuses: nothing changes, nothing is cut off.
    host.accept = false;
    requests = host.requests;
    editor->SetKeyboardVisible(true);
    CHECK(host.requests == requests + 1 && host.frame_at_request.height == 250);
    CHECK(!editor->KeyboardVisible() && !controller->WindowSettings().keyboard);
    CHECK(same(view_size(view), {660, 250}) && same(frame_size(editor), {660, 250}));

    // The same at the smallest zoom.
    host.accept = true;
    editor->ApplySettings({kMinEditorZoom, false});
    CHECK(same(view_size(view), {330, 125}) && same(frame_size(editor), {330, 125}));
    host.accept = false;
    editor->SetKeyboardVisible(true);
    CHECK(!editor->KeyboardVisible() && same(frame_size(editor), {330, 125}));

    // A loaded project brings the keyboard, the host keeps the window: the
    // content shrinks below 50 % to fit; the saved zoom stays as it was.
    editor->ApplySettings({kMinEditorZoom, true});
    CHECK(editor->KeyboardVisible() && same(frame_size(editor), {330, 125}));
    const double display = editor->getFrame()->getTransform().m11;
    CHECK(EditorBaseHeight(true) * display <= 125.0 + 1e-9 && kEditorWidth * display <= 330.0 + 1e-9);
    CHECK(editor->Settings().zoom == kMinEditorZoom);
    // A host that forces a tiny window: still nothing cut off.
    ViewRect tiny(0, 0, 100, 40);
    CHECK(view->onSize(&tiny) == kResultOk);
    const double squeezed = editor->getFrame()->getTransform().m11;
    CHECK(EditorBaseHeight(true) * squeezed <= 40.0 + 1e-9 && kEditorWidth * squeezed <= 100.0 + 1e-9);
    ViewRect nothing(0, 0, 0, 0);
    CHECK(view->onSize(&nothing) != kResultOk && view->onSize(nullptr) != kResultOk);

    // Display scale after opening, and a user drag, host agreeing.
    host.accept = true;
    editor->ApplySettings({1.0, true});
    CHECK(same(view_size(view), {660, 336}));
    CHECK(editor->setContentScaleFactor(1.5f) == kResultOk);
    CHECK(same(view_size(view), {990, 504}) && same(frame_size(editor), {990, 504}));
    CHECK(editor->setContentScaleFactor(0.0f) != kResultOk
          && editor->setContentScaleFactor(std::numeric_limits<float>::quiet_NaN()) != kResultOk);
    ViewRect constrained(0, 0, 5000, 504);
    CHECK(view->checkSizeConstraint(&constrained) == kResultTrue);
    CHECK(constrained.getWidth() == 1980 && constrained.getHeight() == 1008); // 200 % of 1.5
    ViewRect dragged(0, 0, 1320, 672);
    CHECK(view->onSize(&dragged) == kResultOk);
    CHECK(std::fabs(editor->Settings().zoom - 1320.0 / 990.0) < 1e-9);
    CHECK(std::fabs(controller->WindowSettings().zoom - 1320.0 / 990.0) < 1e-9);

    // A second view at the same time, in another window: both follow.
    FakeFrame second_host;
    IPtr<IPlugView> second = open_view(second_host, second_parent);
    Editor* second_editor = as_editor(second);
    CHECK(controller->OpenEditorCount() == 2 && ChildWindows(second_parent) == 1);
    CHECK(second_editor->ShownModel() == 5);
    CHECK(controller->setParamNormalized(kParamTune, 0.3) == kResultOk);
    CHECK(controller->setParamNormalized(kParamModel, ModelNormalized(2)) == kResultOk);
    CHECK(editor->ShownKnobValue(kParamTune) == 0.3f && second_editor->ShownKnobValue(kParamTune) == 0.3f);
    CHECK(editor->ShownModel() == 2 && second_editor->ShownModel() == 2);
    // A screen key reaches the processor through the host's messages.
    CHECK(controller->PlayNote(64));
    {
        ProcessSetup setup{kRealtime, kSample32, 64, 48000.0};
        CHECK(processor->setupProcessing(setup) == kResultOk);
        CHECK(processor->setActive(true) == kResultOk);
        CHECK(controller->PlayNote(64));
        HostProcessData data;
        CHECK(data.prepare(*processor.get(), 64, kSample32));
        data.numSamples = 64;
        CHECK(processor->process(data) == kResultOk);
        CHECK(processor->Engine().TriggerCount() == 1); // the one struck while active
        CHECK(processor->setActive(false) == kResultOk);
    }

    // A knob still held when its window is closed: the host hears the end of
    // that edit, once (review finding TC-R04). Here with removed().
    editor->KnobOf(kParamDamping)->beginEdit();
    editor->KnobOf(kParamDamping)->beginEdit(); // and a wheel turn on top
    editor->KnobOf(kParamTune)->beginEdit();
    CHECK(handler.edits.size() == 2);
    CHECK(view->removed() == kResultOk);
    CHECK(handler.edits.size() == 4 && handler.edits[2].kind == 'e' && handler.edits[3].kind == 'e');
    // One end for each of the two knobs, in the order the window holds them.
    CHECK(handler.edits[2].id == kParamTune && handler.edits[3].id == kParamDamping);
    handler.edits.clear();
    // The first is closed properly, the second stays and still follows.
    CHECK(controller->OpenEditorCount() == 1 && ChildWindows(parent) == 0);
    view = nullptr;
    CHECK(controller->setParamNormalized(kParamTune, 0.6) == kResultOk);
    CHECK(second_editor->ShownKnobValue(kParamTune) == 0.6f);

    // Opened again, with the settings the first one had; and the display
    // scale told before the window exists.
    view = owned(controller->createView(ViewType::kEditor));
    editor = as_editor(view);
    CHECK(view->setFrame(&host) == kResultOk);
    CHECK(editor->setContentScaleFactor(2.0f) == kResultOk);
    const EditorSize expected = EditorSizeFor(2.0 * 1320.0 / 990.0, true);
    CHECK(same(view_size(view), expected)); // asked before opening
    CHECK(view->attached(parent, kPlatformTypeHWND) == kResultOk);
    CHECK(same(view_size(view), expected) && same(frame_size(editor), expected));
    CHECK(controller->OpenEditorCount() == 2);
    // The same view removed and attached once more.
    CHECK(view->removed() == kResultOk && controller->OpenEditorCount() == 1);
    CHECK(view->attached(parent, kPlatformTypeHWND) == kResultOk);
    CHECK(controller->OpenEditorCount() == 2 && ChildWindows(parent) == 1);
    CHECK(editor->ShownKnobValue(kParamTune) == 0.6f);

    // A host that drops a window without removed(): the controller forgets
    // it, its native window goes, and later changes do not reach it. A knob
    // held at that moment still ends its edit.
    editor->KnobOf(kParamVolume)->beginEdit();
    view = nullptr;
    CHECK(handler.edits.size() == 2 && handler.edits[0].kind == 'b' && handler.edits[1].kind == 'e');
    CHECK(handler.edits[1].id == kParamVolume);
    handler.edits.clear();
    CHECK(controller->OpenEditorCount() == 1 && ChildWindows(parent) == 0);
    CHECK(controller->setParamNormalized(kParamVolume, 0.3) == kResultOk);
    CHECK(second_editor->ShownKnobValue(kParamVolume) == 0.3f);
    // A loaded window state reaches the remaining window.
    {
        MemoryStream state;
        const int32 version = kControllerStateVersion;
        const double zoom = 0.75;
        int8 keyboard = 0;
        int32 written = 0;
        state.write(const_cast<int32*>(&version), sizeof(version), &written);
        state.write(const_cast<double*>(&zoom), sizeof(zoom), &written);
        state.write(&keyboard, sizeof(keyboard), &written);
        int64 start = 0;
        state.seek(0, IBStream::kIBSeekSet, &start);
        CHECK(controller->setState(&state) == kResultOk);
        CHECK(!second_editor->KeyboardVisible() && second_editor->Settings().zoom == 0.75);
        CHECK(same(frame_size(second_editor), EditorSizeFor(0.75, false)));
    }
    second = nullptr; // dropped as well
    CHECK(controller->OpenEditorCount() == 0 && ChildWindows(second_parent) == 0);
    CHECK(controller->setParamNormalized(kParamVolume, 0.5) == kResultOk);
    PumpMessages(); // timers and messages of the closed windows find nobody
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    PumpMessages();
    CHECK(handler.edits.empty());
    // The window changes above asked the host to save the project, not more.
    CHECK(handler.dirty >= 1 && handler.restarts == 0);

    CHECK(controller->setComponentHandler(nullptr) == kResultOk);
    processor->disconnect(controller);
    controller->disconnect(processor);
    processor->terminate();
    controller->terminate();
    DestroyWindow(parent);
    DestroyWindow(second_parent);
    DeinitModule();
    std::printf("editor in a window: shows automation, asks before resizing, refusals change "
                "nothing, two views, reopening, scale before and after opening, dropped "
                "windows forgotten\n");
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // progress survives a crash
    TestAgainstFirmware();
    TestBlockSizes();
    TestNoteHandling();
    TestSampleRates();
    TestInstances();
    TestParameterState();
    TestKeyboardQueue();
    TestPauseAndFlush();
    TestActivationAndRateChange();
    TestState();
    TestController();
    TestViewLayout();
    TestEditorResize();
    TestEditorInWindow();
    std::printf("PASS\n");
    return 0;
}
