// Tests of the VST3 plug-in (layer 4) through its interfaces, with small host
// stand-ins: the processor against the separately compiled firmware engine at
// 48 kHz, block sizes and 32/64-bit buffers, host rates, the saved state (round
// trip, damaged and cut states, a missing sample), loading a file while playing
// (no allocation in process), two instances, activation as a reset, the screen
// keyboard, the messages between controller and processor, parameter texts, MIDI
// mapping, buses, and the window's geometry (copied from Kekule's test).
#define TEST_SUPPORT_COUNT_ALLOCATIONS
#include "test_support.h"

#include "controller.h"
#include "display_data.h"
#include "editor.h"
#include "file_drop.h"
#include "map_view.h"
#include "parameters.h"
#include "plugin_ids.h"
#include "processor.h"
#include "reference_api.h"
#include "state_io.h"
#include "value_display.h"
#include "view_layout.h"
#include "widgets.h"
#include "wav_reader.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include <windows.h>
#include <process.h>
#include <xmmintrin.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace messier;
using namespace test_support;
namespace fs = std::filesystem;
using messier_reference::FileImage;
using messier_reference::FirmwareLooper;

// The plug-in's classes are linked into this program; the SDK's DLL entry would
// define these.
void* moduleHandle = nullptr;

namespace {

constexpr int kQ = LooperEngine::kQuantum;
constexpr int kBlockSizes[] = {1, 7, 64, 256, 333, 1024};
constexpr double kRates[] = {44100.0, 88200.0, 96000.0, 192000.0};
constexpr int kVaryingBlocks[] = {64, 1, 333, 12, 7, 2048, 16, 48, 5, 1024, 13, 256, 3, 11, 480};

fs::path g_dir;

std::string Utf8(const fs::path& path) {
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// --- Test material -------------------------------------------------------

FileImage MakeImage(std::uint32_t frames, std::uint32_t rate, std::uint16_t channels,
                    std::uint32_t seed) {
    FileImage image;
    image.frames = frames;
    image.rate = rate;
    image.channels = channels;
    image.pcm.resize(static_cast<std::size_t>(frames) * channels);
    Lcg random(seed);
    for(std::uint32_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / rate;
        for(unsigned c = 0; c < channels; ++c) {
            const double x = 0.3 * std::sin(2.0 * kPi * (c ? 330.0 : 220.0) * t)
                             + 0.3 * std::exp(-std::fmod(t, 0.29) * 30.0) * std::sin(2.0 * kPi * 700.0 * t)
                             + (random.Unit() - 0.5) * 0.04;
            image.pcm[static_cast<std::size_t>(i) * channels + c] =
                static_cast<std::int16_t>(std::lround(std::clamp(x, -1.0, 1.0) * 32767.0));
        }
    }
    return image;
}

std::string WriteWav(const std::string& name, const FileImage& image) {
    const fs::path path = g_dir / name;
    std::ofstream out(path, std::ios::binary);
    auto u16 = [&](unsigned v) {
        const char b[2] = {static_cast<char>(v & 0xff), static_cast<char>((v >> 8) & 0xff)};
        out.write(b, 2);
    };
    auto u32 = [&](std::uint32_t v) {
        u16(v & 0xffff);
        u16(v >> 16);
    };
    const std::uint32_t bytes = static_cast<std::uint32_t>(image.pcm.size() * 2);
    out.write("RIFF", 4);
    u32(36 + bytes);
    out.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(image.channels);
    u32(image.rate);
    u32(image.rate * image.channels * 2);
    u16(image.channels * 2);
    u16(16);
    out.write("data", 4);
    u32(bytes);
    out.write(reinterpret_cast<const char*>(image.pcm.data()), bytes);
    return Utf8(path);
}

// --- A minimal host around one processor ------------------------------------

struct Ev {
    enum Kind { kNoteOn, kNoteOff, kParam };
    std::int64_t sample;
    Kind kind;
    int a;          // pitch or parameter id
    double value;   // velocity (0..1) or normalized value
    int channel = 0;
};
using HostScore = std::vector<Ev>;

struct HostConfig {
    double rate = 48000.0;
    int max_block = 2048;
    bool double_precision = false;
};

class PluginHost {
  public:
    explicit PluginHost(const HostConfig& config, HostScore score = {})
    : config_(config), score_(std::move(score)), events_(4096), out_events_(4096), changes_(64), out_changes_(8) {
        host_app_ = owned(new HostApplication());
        processor = owned(new Processor());
        CHECK(processor->initialize(host_app_) == kResultOk);
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

    void Setup(double rate) {
        config_.rate = rate;
        const int32 size = config_.double_precision ? kSample64 : kSample32;
        CHECK(processor->canProcessSampleSize(size) == kResultTrue);
        ProcessSetup setup{kRealtime, size, config_.max_block, rate};
        CHECK(processor->setupProcessing(setup) == kResultOk);
        data_.unprepare();
        CHECK(data_.prepare(*processor.get(), config_.max_block, size));
        CHECK(data_.numOutputs == 1 && data_.outputs[0].numChannels == 2);
        data_.inputEvents = &events_;
        data_.outputEvents = &out_events_;
        data_.outputParameterChanges = &out_changes_;
        data_.inputParameterChanges = &changes_;
    }

    // Deactivates and activates, and plays the score again from its start.
    void Reactivate() {
        processor->setProcessing(false);
        CHECK(processor->setActive(false) == kResultOk);
        CHECK(processor->setActive(true) == kResultOk);
        processor->setProcessing(true);
        position = 0;
        next_ = 0;
        Clear();
    }

    void Process(int frames) {
        events_.clear();
        out_events_.clear();
        changes_.clearQueue();
        out_changes_.clearQueue();
        const std::int64_t end = position + std::max(frames, 1);
        for(; next_ < score_.size() && score_[next_].sample < end; ++next_) {
            const Ev& ev = score_[next_];
            const auto offset = static_cast<int32>(ev.sample - position);
            if(ev.kind == Ev::kParam) {
                int32 index = 0;
                IParamValueQueue* queue = changes_.addParameterData(static_cast<ParamID>(ev.a), index);
                CHECK(queue && queue->addPoint(offset, ev.value, index) == kResultOk);
            } else {
                Event event{};
                event.sampleOffset = offset;
                if(ev.kind == Ev::kNoteOn) {
                    event.type = Event::kNoteOnEvent;
                    event.noteOn.channel = static_cast<int16>(ev.channel);
                    event.noteOn.pitch = static_cast<int16>(ev.a);
                    event.noteOn.velocity = static_cast<float>(ev.value);
                } else {
                    event.type = Event::kNoteOffEvent;
                    event.noteOff.channel = static_cast<int16>(ev.channel);
                    event.noteOff.pitch = static_cast<int16>(ev.a);
                }
                CHECK(events_.addEvent(event) == kResultOk);
            }
        }
        data_.numSamples = frames;
        data_.inputParameterChanges = external_changes ? external_changes : &changes_;
        {
            const AllocationProbe probe;
            CHECK(processor->process(data_) == kResultOk);
            allocations += probe.Count();
        }
        for(int i = 0; i < frames; ++i) {
            const double l = config_.double_precision ? data_.outputs[0].channelBuffers64[0][i]
                                                      : data_.outputs[0].channelBuffers32[0][i];
            const double r = config_.double_precision ? data_.outputs[0].channelBuffers64[1][i]
                                                      : data_.outputs[0].channelBuffers32[1][i];
            CHECK(std::isfinite(l) && std::isfinite(r));
            CHECK(static_cast<double>(static_cast<float>(l)) == l);
            left.push_back(static_cast<float>(l));
            right.push_back(static_cast<float>(r));
        }
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
    EventList& OutEvents() { return out_events_; }
    ParameterChanges& OutChanges() { return out_changes_; }

    IPtr<Processor> processor;
    std::vector<float> left, right;
    std::int64_t position = 0;
    std::uint64_t allocations = 0;
    // If set, the parameter changes of every block come from here, as a host's
    // edit queue would deliver them, instead of from the score.
    IParameterChanges* external_changes = nullptr;

  private:
    IPtr<HostApplication> host_app_;
    HostConfig config_;
    HostScore score_;
    std::size_t next_ = 0;
    HostProcessData data_;
    EventList events_, out_events_;
    ParameterChanges changes_, out_changes_;
};

struct VaryingBlocks {
    std::size_t at = 0;
    int operator()() { return kVaryingBlocks[at++ % std::size(kVaryingBlocks)]; }
};

// --- States --------------------------------------------------------------

std::vector<char> SaveState(IComponent& component) {
    MemoryStream stream;
    CHECK(component.getState(&stream) == kResultOk);
    return std::vector<char>(stream.getData(), stream.getData() + stream.getSize());
}

tresult LoadState(IComponent& component, const std::vector<char>& bytes) {
    MemoryStream stream;
    int32 written = 0;
    if(!bytes.empty())
        stream.write(const_cast<char*>(bytes.data()), static_cast<int32>(bytes.size()), &written);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    return component.setState(&stream);
}

std::vector<char> MakeState(const std::array<double, kSavedParamCount>& values, const std::string& path) {
    MemoryStream stream;
    SavedState saved;
    saved.values = values;
    saved.path = path;
    CHECK(WriteSavedState(&stream, saved));
    return std::vector<char>(stream.getData(), stream.getData() + stream.getSize());
}

std::array<double, kSavedParamCount> DefaultValues() {
    std::array<double, kSavedParamCount> values{};
    for(int i = 0; i < kSavedParamCount; ++i)
        values[static_cast<std::size_t>(i)] =
            kDefaults[static_cast<std::size_t>(kSavedIndex[static_cast<std::size_t>(i)])];
    return values;
}

// The start values of the comparisons: every pot off its default.
std::array<double, kSavedParamCount> StartValues() {
    std::array<double, kSavedParamCount> v = DefaultValues();
    const double pots[8] = {0.1, 0.55, 0.85, 0.45, 0.5, 0.6, 0.4, 0.35};
    const double secondaries[6] = {0.05, 0.3, 0.05, 0.1, 0.2, 0.1};
    for(int i = 0; i < 8; ++i)
        v[static_cast<std::size_t>(i)] = pots[i];
    for(int i = 0; i < 6; ++i)
        v[static_cast<std::size_t>(8 + i)] = secondaries[i];
    return v;
}

// A performance in host samples: notes on four channels, every pot and secondary
// moving, the wheel, Freeze and Reset. Volume stays at 100 % (the firmware has
// none).
HostScore Performance(double rate, std::int64_t total, std::uint32_t seed) {
    HostScore score;
    Lcg random(seed);
    static constexpr ParamID kMoving[] = {100, 101, 102, 103, 104, 105, 106, 107,
                                          110, 112, 113, 114, 115, 117};
    int held[4] = {-1, -1, -1, -1};
    for(std::int64_t t : {0, 1, 63, 64, 65, 333, 1023, 1024})
        score.push_back({t, Ev::kNoteOn, 48 + static_cast<int>(t % 24), 0.8, static_cast<int>(t % 4)});
    std::int64_t t = static_cast<std::int64_t>(2000 * rate / 48000.0);
    while(true) {
        t += 1 + static_cast<std::int64_t>(random.Below(900) * rate / 48000.0);
        if(t >= total)
            break;
        const std::uint32_t kind = random.Below(20);
        const int channel = static_cast<int>(random.Below(4));
        if(kind < 3) {
            held[channel] = 36 + static_cast<int>(random.Below(60));
            score.push_back({t, Ev::kNoteOn, held[channel], (1 + random.Below(127)) / 127.0, channel});
        } else if(kind < 5) {
            if(held[channel] >= 0)
                score.push_back({t, Ev::kNoteOff, held[channel], 0.0, channel});
            held[channel] = -1;
        } else if(kind < 15) {
            const ParamID id = kMoving[random.Below(14)];
            double value = random.Unit();
            if(id == kParamDisplacement)
                value *= 0.3;
            if(id == kParamDensity)
                value = 0.2 + 0.6 * value;
            score.push_back({t, Ev::kParam, static_cast<int>(id), value});
        } else if(kind < 17) {
            score.push_back({t, Ev::kParam, static_cast<int>(kParamPitchBend), random.Unit()});
        } else if(kind == 17) {
            score.push_back({t, Ev::kParam, static_cast<int>(kParamFreeze), random.Below(2) ? 1.0 : 0.0});
        } else if(kind == 18) {
            score.push_back({t, Ev::kParam, static_cast<int>(kParamReset), 1.0});
            score.push_back({t + 5, Ev::kParam, static_cast<int>(kParamReset), 0.0});
        }
    }
    std::stable_sort(score.begin(), score.end(), [](const Ev& a, const Ev& b) { return a.sample < b.sample; });
    return score;
}

class FlushDenormals {
  public:
    FlushDenormals() : saved_(_mm_getcsr()) { _mm_setcsr(saved_ | 0x8040u); }
    ~FlushDenormals() { _mm_setcsr(saved_); }

  private:
    unsigned int saved_;
};

// The same performance on the firmware engine: every event on the quantum that
// starts at or after its sample, values as the processor maps them.
std::vector<float> RenderFirmware(const HostScore& score, std::int64_t total, const FileImage& image) {
    const FlushDenormals flush;
    FirmwareLooper firmware(image.pcm.size());
    const std::array<double, kSavedParamCount> start = StartValues();
    float primary[8], secondary[8] = {};
    // The pots as the plug-in gives them to the core (PotOf, since 0.1.8).
    for(int i = 0; i < 8; ++i)
        primary[i] = PotOf(kFirstPrimary + static_cast<std::uint32_t>(i), start[static_cast<std::size_t>(i)]);
    const int secondary_index[6] = {0, 2, 3, 4, 5, 7};
    for(int i = 0; i < 6; ++i)
        secondary[secondary_index[i]] = PotOf(kParamIds[static_cast<std::size_t>(8 + i)], start[static_cast<std::size_t>(8 + i)]);
    firmware.Reset(&image, primary, secondary);
    for(std::uint8_t c = 0; c < 16; ++c)
        firmware.Midi(0xe0, c, 0, 64); // the wheel's centre, as the plug-in sends it
    std::vector<float> left(static_cast<std::size_t>(total + kQ)), right(left.size());
    std::size_t next = 0;
    bool reset_pressed = false;
    for(std::int64_t q = 0; q * kQ < total; ++q) {
        for(; next < score.size() && (score[next].sample + kQ - 1) / kQ <= q; ++next) {
            const Ev& ev = score[next];
            if(ev.kind == Ev::kNoteOn) {
                const long v = std::clamp(std::lround(ev.value * 127.0), 1L, 127L);
                firmware.Midi(0x90, static_cast<std::uint8_t>(ev.channel), static_cast<std::uint8_t>(ev.a),
                              static_cast<std::uint8_t>(v));
            } else if(ev.kind == Ev::kNoteOff) {
                firmware.Midi(0x80, static_cast<std::uint8_t>(ev.channel), static_cast<std::uint8_t>(ev.a), 0);
            } else {
                const auto id = static_cast<std::uint32_t>(ev.a);
                const float value = PotOf(id, ev.value);
                if(IsPrimary(id))
                    firmware.SetPrimary(id - kFirstPrimary, value);
                else if(IsSecondary(id))
                    firmware.SetSecondary(id - kFirstSecondary, value);
                else if(id == kParamFreeze)
                    firmware.Freeze(IsOn(ev.value));
                else if(id == kParamReset) {
                    if(IsOn(ev.value) && !reset_pressed)
                        firmware.ResetPlayhead();
                    reset_pressed = IsOn(ev.value);
                } else if(id == kParamPitchBend) {
                    const int raw = PitchBendRaw(ev.value);
                    for(std::uint8_t c = 0; c < 16; ++c)
                        firmware.Midi(0xe0, c, static_cast<std::uint8_t>(raw & 0x7f),
                                      static_cast<std::uint8_t>(raw >> 7));
                }
            }
        }
        firmware.Render(left.data() + q * kQ, right.data() + q * kQ, kQ);
    }
    left.resize(static_cast<std::size_t>(total));
    right.resize(static_cast<std::size_t>(total));
    left.insert(left.end(), right.begin(), right.end());
    return left;
}

FileImage g_image;      // stereo 48 kHz
FileImage g_image_mono; // mono 44.1 kHz
std::string g_path, g_path_mono;

std::vector<float> RenderPlugin(const HostConfig& config, const HostScore& score, std::int64_t total,
                                int block, std::uint64_t* allocations = nullptr) {
    PluginHost host(config, score);
    CHECK(LoadState(*host.processor, MakeState(StartValues(), g_path)) == kResultOk);
    if(block > 0)
        host.Run(total, block);
    else
        host.Run(total, VaryingBlocks{});
    if(allocations)
        *allocations += host.allocations;
    return host.Stereo();
}

void TestAgainstFirmware() {
#ifdef NDEBUG
    const std::int64_t total = 10 * 48000;
#else
    const std::int64_t total = 48000;
#endif
    const HostScore score = Performance(48000.0, total, 7);
    std::uint64_t allocations = 0;
    const std::vector<float> ours = RenderPlugin({}, score, total, 0, &allocations);
    const std::vector<float> theirs = RenderFirmware(score, total, g_image);
    std::size_t differing = 0;
    for(std::size_t i = 0; i < ours.size(); ++i)
        differing += std::memcmp(&ours[i], &theirs[i], sizeof(float)) != 0;
    std::printf("firmware: %zu events over %lld samples at 48 kHz, varying blocks: %zu of %zu "
                "samples differ, rms %.4f, %llu allocations in process\n",
                score.size(), static_cast<long long>(total), differing, ours.size(), Rms(ours),
                static_cast<unsigned long long>(allocations));
    CHECK(differing == 0 && Rms(ours) > 0.01 && allocations == 0);
}

void TestBlockSizes() {
#ifdef NDEBUG
    const std::int64_t seconds = 4;
#else
    const std::int64_t seconds = 1;
#endif
    for(const double rate : {48000.0, 44100.0}) {
        const auto total = static_cast<std::int64_t>(seconds * rate);
        const HostScore score = Performance(rate, total, 9);
        HostConfig config;
        config.rate = rate;
        const std::vector<float> reference = RenderPlugin(config, score, total, 64);
        std::uint64_t allocations = 0;
        for(const bool wide : {false, true}) {
            config.double_precision = wide;
            for(const int block : kBlockSizes)
                CHECK(RenderPlugin(config, score, total, block, &allocations) == reference);
            CHECK(RenderPlugin(config, score, total, 0, &allocations) == reference);
        }
        CHECK(allocations == 0);
    }
    std::printf("block sizes: 1, 7, 64, 256, 333, 1024 and varying, 32 and 64 bit: identical at "
                "48 and 44.1 kHz\n");
}

void TestSampleRates() {
    for(const double rate : kRates) {
        HostConfig config;
        config.rate = rate;
        PluginHost host(config, Performance(rate, static_cast<std::int64_t>(2 * rate), 3));
        CHECK(LoadState(*host.processor, MakeState(StartValues(), g_path_mono)) == kResultOk);
        host.Run(static_cast<std::int64_t>(2 * rate), VaryingBlocks{});
        const uint32 latency = host.processor->getLatencySamples();
        CHECK(static_cast<int>(latency) == host.processor->Adapter().LatencySamples() && latency > 0);
        CHECK(host.left.size() == static_cast<std::size_t>(2 * rate));
        // The engine renders 48 kHz whatever the host's rate, as far as the output needs.
        const double internal = host.processor->Adapter().RenderedSamples();
        CHECK(std::fabs(internal - 2 * 48000.0) < 300.0);
        CHECK(host.allocations == 0 && Rms(host.left) > 0.005);
        std::printf("  %g Hz: latency %u samples reported, %zu samples out, %.0f rendered at 48 kHz, "
                    "peak %.3f\n",
                    rate, latency, host.left.size(), internal, Peak(host.left));
    }
    std::printf("rates: output length exact, the reported latency is the adapter's (measured in "
                "messier.adapter), no allocation\n");
}

void TestActivation() {
    const std::int64_t total = 48000;
    const HostScore score = Performance(48000.0, total, 5);
    PluginHost host({}, score);
    CHECK(LoadState(*host.processor, MakeState(StartValues(), g_path)) == kResultOk);
    host.Run(total, VaryingBlocks{});
    const std::vector<float> first = host.Stereo();
    // The second run starts from the current state, which the automation has moved:
    // put the start values back first.
    CHECK(LoadState(*host.processor, MakeState(StartValues(), g_path)) == kResultOk);
    host.Reactivate();
    host.Run(total, VaryingBlocks{});
    CHECK(host.Stereo() == first);
    std::printf("activation: restarts the firmware engine with its seed; the same project renders "
                "the same every time\n");
}

void TestTwoInstances() {
    const std::int64_t total = 2 * 48000;
    const HostScore score = Performance(48000.0, total, 11);
    const std::vector<float> alone = RenderPlugin({}, score, total, 256);
    std::vector<float> with_other;
    std::thread other([&] {
        HostConfig config;
        config.rate = 44100.0;
        PluginHost b(config, Performance(44100.0, 3 * 44100, 12));
        CHECK(LoadState(*b.processor, MakeState(DefaultValues(), g_path_mono)) == kResultOk);
        b.Run(3 * 44100, 128);
    });
    with_other = RenderPlugin({}, score, total, 256);
    other.join();
    CHECK(with_other == alone);
    std::printf("two instances: one renders the same while another plays in a second thread\n");
}

// --- State and files -----------------------------------------------------

template <typename Predicate>
bool WaitFor(Predicate predicate) {
    for(int i = 0; i < 1000; ++i) {
        if(predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

void TestState() {
    PluginHost host({});
    const std::vector<char> start = MakeState(StartValues(), g_path);
    CHECK(LoadState(*host.processor, start) == kResultOk);
    CHECK(SaveState(*host.processor) == start); // a round trip, path included
    host.Run(4800);
    CHECK(host.processor->Engine().CurrentFile() && host.processor->Engine().CurrentFile()->frames == g_image.frames);

    // Damaged states change nothing.
    std::vector<std::vector<char>> bad;
    for(std::size_t length = 0; length < start.size(); ++length)
        bad.emplace_back(start.begin(), start.begin() + static_cast<std::ptrdiff_t>(length));
    auto patched = [&](std::size_t at, const void* bytes, std::size_t n) {
        std::vector<char> s = start;
        std::memcpy(s.data() + at, bytes, n);
        return s;
    };
    const int32 version2 = 3, minus = -1, huge = kMaxPathBytes + 1; // 3: a version not known (2 is current since 0.1.8)
    const double nan = std::nan(""), big = 1.5, negative = -0.25;
    bad.push_back(patched(0, &version2, 4));
    bad.push_back(patched(4, &nan, 8));
    bad.push_back(patched(4 + 8 * 3, &big, 8));
    bad.push_back(patched(4 + 8 * 15, &negative, 8));
    const std::size_t length_at = 4 + 8 * kSavedParamCount;
    bad.push_back(patched(length_at, &minus, 4));
    bad.push_back(patched(length_at, &huge, 4));
    std::vector<char> utf8 = MakeState(StartValues(), "C:\\a\xC0\xAF.wav");
    bad.push_back(utf8);
    std::vector<char> nul = MakeState(StartValues(), std::string("C:\\a\0b.wav", 10));
    bad.push_back(nul);
    for(const std::vector<char>& state : bad) {
        CHECK(LoadState(*host.processor, state) == kResultFalse);
        CHECK(SaveState(*host.processor) == start);
    }
    host.Run(4800);
    CHECK(host.processor->Engine().CurrentFile()->frames == g_image.frames);

    // The controller reads the same state, and refuses the same damage.
    IPtr<HostApplication> app = owned(new HostApplication());
    IPtr<Controller> controller = owned(new Controller());
    CHECK(controller->initialize(app) == kResultOk);
    MemoryStream stream;
    int32 written = 0;
    stream.write(const_cast<char*>(start.data()), static_cast<int32>(start.size()), &written);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    CHECK(controller->setComponentState(&stream) == kResultOk);
    for(int i = 0; i < kSavedParamCount; ++i)
        CHECK(controller->getParamNormalized(kParamIds[static_cast<std::size_t>(kSavedIndex[static_cast<std::size_t>(i)])])
              == StartValues()[static_cast<std::size_t>(i)]);
    CHECK(controller->File().path == g_path);
    for(const std::vector<char>& state : bad) {
        MemoryStream damaged;
        if(!state.empty())
            damaged.write(const_cast<char*>(state.data()), static_cast<int32>(state.size()), &written);
        damaged.seek(0, IBStream::kIBSeekSet, nullptr);
        CHECK(controller->setComponentState(&damaged) == kResultFalse);
    }
    // The controller's own state: round trip and refusals.
    MemoryStream own;
    CHECK(controller->getState(&own) == kResultOk);
    own.seek(0, IBStream::kIBSeekSet, nullptr);
    CHECK(controller->setState(&own) == kResultOk);
    CHECK(controller->terminate() == kResultOk);

    // A project whose sample is missing: silent, the path kept, the reason known.
    const std::string gone = Utf8(g_dir / "moved away.wav");
    const std::vector<char> missing = MakeState(StartValues(), gone);
    CHECK(LoadState(*host.processor, missing) == kResultOk);
    host.Clear();
    host.Run(9600);
    CHECK(Peak(host.left, 4800) == 0.0f && Peak(host.right, 4800) == 0.0f);
    CHECK(SaveState(*host.processor) == missing);
    const FileStatus status = host.processor->Loader().Status();
    CHECK(status.path == gone && status.sound == SoundState::kMissing);
    // A state without a sample: none.
    CHECK(LoadState(*host.processor, MakeState(DefaultValues(), "")) == kResultOk);
    CHECK(host.processor->Loader().Status().sound == SoundState::kNone);
    host.Run(256);
    CHECK(host.allocations == 0);
    std::printf("state: round trip with the path; %zu damaged or cut states refused by processor "
                "and controller; a missing sample leaves silence and keeps its path\n",
                bad.size());
}

// The file is read on the export path: setState before the first block, and the
// first block already plays it (after the core's 256-sample fade-in).
void TestExportStart() {
    PluginHost host({});
    CHECK(LoadState(*host.processor, MakeState(StartValues(), g_path)) == kResultOk);
    host.Run(1024, 1024);
    CHECK(Rms(host.left, 256, 1024) > 0.01);
    std::printf("export: the sample of a just loaded project sounds from the first block (rms %.3f "
                "after the fade-in)\n",
                Rms(host.left, 256, 1024));
}

// Loading a file from the window while playing: the processor allocates nothing,
// the new file plays after the swap, the old one is freed on the loader's thread.
void TestLoadWhilePlaying() {
    PluginHost host({});
    CHECK(LoadState(*host.processor, MakeState(StartValues(), g_path)) == kResultOk);
    host.Run(48000, 256);
    // As the controller sends it.
    IPtr<IMessage> message = owned(new HostMessage());
    message->setMessageID(kLoadMessageId);
    message->getAttributes()->setBinary(kAttrPath, g_path_mono.data(), static_cast<uint32>(g_path_mono.size()));
    CHECK(host.processor->notify(message) == kResultOk);
    CHECK(WaitFor([&] {
        host.Run(256, 256);
        return host.processor->Engine().CurrentFile()
               && host.processor->Engine().CurrentFile()->channels == 1;
    }));
    host.Run(48000, 256);
    CHECK(host.allocations == 0);
    CHECK(host.processor->Loader().SoundPath() == g_path_mono);
    CHECK(SaveState(*host.processor) == MakeState(StartValues(), g_path_mono));
    // A bad file from the window: the old one plays on.
    const std::string bad = Utf8(g_dir / "bad.wav");
    {
        std::ofstream out(fs::path(g_dir / "bad.wav"), std::ios::binary);
        out << "RIFF0000WAVEnothing";
    }
    message->getAttributes()->setBinary(kAttrPath, bad.data(), static_cast<uint32>(bad.size()));
    CHECK(host.processor->notify(message) == kResultOk);
    CHECK(WaitFor([&] { return host.processor->Loader().Idle(); }));
    host.Run(4800);
    CHECK(host.processor->Engine().CurrentFile()->channels == 1);
    CHECK(host.processor->Loader().Status().request_state == RequestState::kFailed);
    // Refused messages.
    message->getAttributes()->setBinary(kAttrPath, "", 0);
    CHECK(host.processor->notify(message) == kInvalidArgument);
    std::printf("load while playing: swapped in without allocation in process; a bad file keeps the "
                "old one\n");
}

// --- Controller, messages, keyboard ----------------------------------------

std::string Text(Controller& controller, ParamID id, double normalized) {
    String128 text{};
    CHECK(controller.getParamStringByValue(id, normalized, text) == kResultOk);
    return StringConvert::convert(text);
}

void TestController() {
    IPtr<HostApplication> app = owned(new HostApplication());
    IPtr<Controller> controller = owned(new Controller());
    CHECK(controller->initialize(app) == kResultOk);
    CHECK(controller->getParameterCount() == kParamCount);
    for(int i = 0; i < kParamCount; ++i) {
        ParameterInfo info{};
        CHECK(controller->getParameterInfo(i, info) == kResultOk);
        CHECK(Index(info.id) >= 0);
        CHECK(info.defaultNormalizedValue == kDefaults[static_cast<std::size_t>(Index(info.id))]);
        // Every shown value reads back to a value that shows the same.
        for(int k = 0; k <= 200; ++k) {
            const double v = k / 200.0;
            const std::string shown = Text(*controller, info.id, v);
            ParamValue back = -1.0;
            CHECK(controller->getParamValueByString(info.id, const_cast<TChar*>(StringConvert::convert(shown).c_str()) , back) == kResultOk);
            // Window between two shapes shows the nearest with "~"; typing that picks the shape.
            if(info.id == kParamWindow && shown.front() == '~')
                CHECK(Text(*controller, info.id, back) == shown.substr(1));
            else
                CHECK(Text(*controller, info.id, back) == shown);
        }
    }
    CHECK(Text(*controller, kParamSpeed, 0.5) == "+0.00" && Text(*controller, kParamSpeed, 7.0 / 12.0) == "+1.00");
    CHECK(Text(*controller, kParamPitchRandom, 1.0) == "\xC2\xB1" "12.0" && Text(*controller, kParamPitchRandom, 0.25) == "\xC2\xB1" "3.0");
    CHECK(Text(*controller, kParamWindow, 0.55) == "~Bartlett" && Text(*controller, kParamWindow, 0.6) == "~Ramp Up");
    CHECK(Text(*controller, kParamPitch, 0.5) == "+0.00");
    CHECK(Text(*controller, kParamPitch, 0.0) == "-3.00" && Text(*controller, kParamPitch, 1.0) == "+2.00");
    CHECK(Text(*controller, kParamWindow, 0.0) == "Gaussian" && Text(*controller, kParamWindow, 1.0) == "Rectangle");
    CHECK(Text(*controller, kParamWindow, 0.5) == "Bartlett");
    CHECK(Text(*controller, kParamSize, 0.5) == "25.0");
    CHECK(Text(*controller, kParamSpray, 0.5) == "\xC2\xB1" "25.0");
    CHECK(Text(*controller, kParamFreeze, 1.0) == "On");
    double v = -1;
    CHECK(ParseValue(kParamSpeed, "1x", v) && std::fabs(v - 7.0 / 12.0) < 1e-12);
    CHECK(ParseValue(kParamSpeed, "0", v) && v == 0.5);
    CHECK(ParseValue(kParamPitchRandom, "+-6 st", v) && v == 0.5);
    CHECK(ParseValue(kParamWindow, "~Ramp Up", v) && std::fabs(v - 4.0 / 6.0) < 1e-12);
    CHECK(ParseValue(kParamWindow, "rect", v) && v == 1.0);
    CHECK(ParseValue(kParamWindow, "4", v) && v == 0.5);
    CHECK(ParseValue(kParamSpray, "+-25 %", v) && std::fabs(v - 0.5) < 1e-12);
    CHECK(!ParseValue(kParamStart, "12junk", v) && !ParseValue(kParamFreeze, "maybe", v));
    // MIDI controllers: 20..27 the pots, 33 Volume, the wheel; nothing else.
    for(int cc = 0; cc < 130; ++cc) {
        ParamID id = 0;
        const bool mapped = controller->getMidiControllerAssignment(0, 3, static_cast<CtrlNumber>(cc), id) == kResultTrue;
        if(cc >= 20 && cc <= 27)
            CHECK(mapped && id == static_cast<ParamID>(100 + cc - 20));
        else if(cc == 33)
            CHECK(mapped && id == kParamVolume);
        else if(cc == kPitchBend)
            CHECK(mapped && id == kParamPitchBend);
        else
            CHECK(!mapped);
    }
    CHECK(controller->terminate() == kResultOk);
    std::printf("controller: %d parameters, texts read back for 201 values each, CC 20-27 and 33, "
                "the wheel\n",
                kParamCount);
}

void TestMessagesAndKeyboard() {
    IPtr<HostApplication> app = owned(new HostApplication());
    PluginHost host({});
    IPtr<Controller> controller = owned(new Controller());
    CHECK(controller->initialize(app) == kResultOk);
    // The host connects the two halves.
    auto* processor_point = static_cast<IConnectionPoint*>(host.processor.get());
    auto* controller_point = static_cast<IConnectionPoint*>(controller.get());
    CHECK(processor_point->connect(controller_point) == kResultOk);
    CHECK(controller_point->connect(processor_point) == kResultOk);

    // Load from the window: "loading" at once, then "ready" on request.
    CHECK(controller->LoadFile(g_path));
    CHECK(controller->File().request_state == RequestState::kLoading || controller->File().sound == SoundState::kReady);
    CHECK(WaitFor([&] { return host.processor->Loader().Idle(); }));
    CHECK(controller->RequestFileStatus());
    CHECK(controller->File().sound == SoundState::kReady && controller->File().path == g_path
          && controller->File().request_state == RequestState::kNone
          && controller->File().info == "48 kHz stereo, 16 bit, 0:03.0");
    CHECK(!controller->LoadFile(""));
    host.Run(4800, 256);

    // The screen keyboard: note 72 sounds an octave up, its release returns.
    CHECK(controller->PlayNote(72));
    host.Run(256, 256);
    CHECK(host.processor->Engine().EffectivePitch() == 2.0f);
    bool note_on_out = false;
    for(int32 i = 0; i < host.OutEvents().getEventCount(); ++i) {
        Event e{};
        host.OutEvents().getEvent(i, e);
        note_on_out |= e.type == Event::kNoteOnEvent && e.noteOn.pitch == 72;
    }
    CHECK(note_on_out);
    CHECK(controller->ReleaseNote(72));
    host.Run(256, 256);
    CHECK(host.processor->Engine().EffectivePitch() == 1.0f);
    bool note_off_out = false;
    for(int32 i = 0; i < host.OutEvents().getEventCount(); ++i) {
        Event e{};
        host.OutEvents().getEvent(i, e);
        note_off_out |= e.type == Event::kNoteOffEvent && e.noteOff.pitch == 72;
    }
    CHECK(note_off_out);
    // A MIDI note with the wheel: the wheel bends the held note, by +-2 semitones.
    HostScore score = {{host.position, Ev::kNoteOn, 60, 1.0, 5},
                       {host.position, Ev::kParam, static_cast<int>(kParamPitchBend), 1.0}};
    PluginHost second({}, score);
    CHECK(LoadState(*second.processor, MakeState(DefaultValues(), g_path)) == kResultOk);
    second.Run(host.position + 512, 256);
    CHECK(std::fabs(second.processor->Engine().EffectivePitch() - std::pow(2.0f, (2.0f * 8191 / 8192) / 12.0f)) < 1e-6f);

    CHECK(processor_point->disconnect(controller_point) == kResultOk);
    CHECK(controller_point->disconnect(processor_point) == kResultOk);
    CHECK(controller->terminate() == kResultOk);
    CHECK(host.allocations == 0);
    std::printf("messages: load and status between the two halves; the screen keyboard plays and "
                "releases (MIDI out both ways); the wheel bends the held note\n");
}

void TestBuses() {
    IPtr<HostApplication> app = owned(new HostApplication());
    IPtr<Processor> processor = owned(new Processor());
    CHECK(processor->initialize(app) == kResultOk);
    CHECK(processor->getBusCount(kAudio, kInput) == 1 && processor->getBusCount(kAudio, kOutput) == 1);
    BusInfo info{};
    CHECK(processor->getBusInfo(kAudio, kInput, 0, info) == kResultOk);
    CHECK(info.busType == kAux && (info.flags & BusInfo::kDefaultActive) == 0 && info.channelCount == 2);
    SpeakerArrangement stereo = SpeakerArr::kStereo, mono = SpeakerArr::kMono, surround = SpeakerArr::k51;
    SpeakerArrangement ins[2] = {stereo, stereo};
    CHECK(processor->setBusArrangements(&stereo, 1, &stereo, 1) == kResultTrue);
    CHECK(processor->setBusArrangements(&mono, 1, &stereo, 1) == kResultTrue);
    CHECK(processor->getBusInfo(kAudio, kInput, 0, info) == kResultOk && info.channelCount == 1);
    CHECK(processor->setBusArrangements(nullptr, 0, &stereo, 1) == kResultTrue);
    CHECK(processor->setBusArrangements(&surround, 1, &stereo, 1) == kResultFalse);
    CHECK(processor->setBusArrangements(&stereo, 1, &mono, 1) == kResultFalse);
    CHECK(processor->setBusArrangements(ins, 2, &stereo, 1) == kResultFalse);
    CHECK(processor->getTailSamples() == kInfiniteTail);
    CHECK(processor->terminate() == kResultOk);
    std::printf("buses: stereo out; a sidechain input (aux, inactive by default) stereo, mono or "
                "none; infinite tail\n");
}

// --- The window's geometry (from Kekule's test, for Messier's sizes) --------

// Drag and drop (0.1.8): which file a drop loads, and what the drop target tells the
// window while a drag passes. The real drag from the Explorer is the DAW test's.
class FakePackage : public VSTGUI::IDataPackage {
  public:
    std::vector<std::pair<Type, std::string>> items;
    uint32_t getCount() const override { return static_cast<uint32_t>(items.size()); }
    uint32_t getDataSize(uint32_t i) const override { return static_cast<uint32_t>(items[i].second.size()); }
    Type getDataType(uint32_t i) const override { return items[i].first; }
    uint32_t getData(uint32_t i, const void*& buffer, Type& type) const override {
        buffer = items[i].second.data();
        type = items[i].first;
        return static_cast<uint32_t>(items[i].second.size());
    }
};

void TestFileDrop() {
    using VSTGUI::IDataPackage;
    FakePackage none;
    CHECK(PickDroppedWav(&none).empty() && PickDroppedWav(nullptr).empty());
    FakePackage mixed;
    mixed.items = {{IDataPackage::kText, "C:/text/looks-like.wav"},
                   {IDataPackage::kFilePath, "C:/samples/readme.txt"},
                   {IDataPackage::kFilePath, std::string("C:/samples/Gr\xc3\xbc" "n.WAV") + '\0'},
                   {IDataPackage::kFilePath, "C:/samples/second.wav"},
                   {IDataPackage::kFilePath, "C:/samples/.wa"}};
    CHECK(PickDroppedWav(&mixed) == "C:/samples/Gr\xc3\xbc" "n.WAV"); // the first WAV, any case, UTF-8
    FakePackage text;
    text.items = {{IDataPackage::kFilePath, "C:/a.wave"}, {IDataPackage::kFilePath, "C:/b.wav.txt"}};
    CHECK(PickDroppedWav(&text).empty());

    // Judged by the WAV's headers while it hovers (0.1.8): a damaged WAV, a missing one
    // and a text file are refused with a reason, before anything is dropped.
    const std::string bad = Utf8(g_dir / "drop-bad.wav");
    {
        std::ofstream out(fs::path(g_dir / "drop-bad.wav"), std::ios::binary);
        out << "RIFF0000WAVEnothing";
    }
    FakePackage usable;
    usable.items = {{IDataPackage::kText, "C:/text/looks-like.wav"},
                    {IDataPackage::kFilePath, bad},
                    {IDataPackage::kFilePath, Utf8(g_dir / "readme.txt")},
                    {IDataPackage::kFilePath, g_path}};
    const DropVerdict ok = JudgeDrop(&usable);
    CHECK(ok.usable && ok.path == g_path); // the first WAV that would load
    FakePackage damaged;
    damaged.items = {{IDataPackage::kFilePath, bad}};
    const DropVerdict refused = JudgeDrop(&damaged);
    CHECK(!refused.usable && refused.reason.find("not a valid WAV") == 0);
    FakePackage missing;
    missing.items = {{IDataPackage::kFilePath, Utf8(g_dir / "gone.wav")}};
    CHECK(JudgeDrop(&missing).reason.find("file not found") == 0);
    CHECK(JudgeDrop(&text).reason == "not a WAV file" && !JudgeDrop(nullptr).usable);

    std::vector<std::pair<FileDropTarget::Hover, std::string>> shown;
    std::string dropped;
    auto target = VSTGUI::makeOwned<FileDropTarget>(
        [&](FileDropTarget::Hover state, const std::string& reason) { shown.push_back({state, reason}); },
        [&](const std::string& path) { dropped = path; });
    const VSTGUI::DragEventData with{&usable, VSTGUI::CPoint(), VSTGUI::Modifiers()};
    const VSTGUI::DragEventData without{&damaged, VSTGUI::CPoint(), VSTGUI::Modifiers()};
    CHECK(target->onDragEnter(without) == VSTGUI::DragOperation::None);
    CHECK(shown.back().first == FileDropTarget::Hover::kRefused && shown.back().second == refused.reason);
    CHECK(target->onDragMove(without) == VSTGUI::DragOperation::None);
    target->onDragLeave(without);
    CHECK(shown.back().first == FileDropTarget::Hover::kNone);
    CHECK(!target->onDrop(without) && dropped.empty());
    CHECK(target->onDragEnter(with) == VSTGUI::DragOperation::Copy && shown.back().first == FileDropTarget::Hover::kUsable);
    CHECK(target->onDragMove(with) == VSTGUI::DragOperation::Copy);
    CHECK(target->onDrop(with) && dropped == g_path && shown.back().first == FileDropTarget::Hover::kNone);
    dropped.clear();
    CHECK(target->onDrop(with) && dropped == g_path); // a drop without an enter is judged too
    // Callbacks that throw: the drop reports failure, nothing leaves.
    auto throwing = VSTGUI::makeOwned<FileDropTarget>(
        [](FileDropTarget::Hover, const std::string&) { throw std::bad_alloc(); },
        [](const std::string&) { throw std::bad_alloc(); });
    CHECK(throwing->onDragEnter(with) == VSTGUI::DragOperation::Copy);
    CHECK(!throwing->onDrop(with));
    std::printf("drag and drop: the first .wav that would load is taken; damaged, missing and other "
                "files are refused while they hover, with the reason\n");
}

// LATCH on the screen keyboard (0.1.8): a struck key stays held after the mouse is
// let go; another key moves the hold, the same key or LATCH off lets go. Without
// latch, letting go of the mouse lets go of the key, as before.
void TestKeyboardLatch() {
    using namespace VSTGUI;
    std::vector<std::pair<char, int>> calls;
    auto view = makeOwned<KeyboardView>(CRect(0, 0, 700, 60), [&](int n) { calls.push_back({'+', n}); },
                                        [&](int n) { calls.push_back({'-', n}); });
    auto click = [&](int note) {
        view->MouseDownOn(note);
        view->MouseUp();
    };
    click(60); // no latch: struck and let go
    CHECK(view->Held() == -1 && calls.size() == 2 && calls[0] == std::make_pair('+', 60) && calls[1] == std::make_pair('-', 60));
    calls.clear();
    view->SetLatch(true);
    click(64); // held after the mouse is let go
    CHECK(view->Held() == 64 && calls.size() == 1 && calls[0] == std::make_pair('+', 64));
    click(67); // the hold moves: 64 let go, 67 struck
    CHECK(view->Held() == 67 && calls.size() == 3 && calls[1] == std::make_pair('-', 64) && calls[2] == std::make_pair('+', 67));
    click(67); // the same key again: let go
    CHECK(view->Held() == -1 && calls.size() == 4 && calls[3] == std::make_pair('-', 67));
    click(72);
    view->SetLatch(false); // LATCH off: let go
    CHECK(view->Held() == -1 && calls.size() == 6 && calls[5] == std::make_pair('-', 72));
    std::printf("keyboard latch: held after the mouse, moved by another key, let go by the same key "
                "or LATCH off; without it, as before\n");
}

// Stop and Bypass (0.1.8): the output fades out in 5 ms, then the engine rests (its
// time stands); released, it goes on where it stopped and fades in. Bypass is saved
// (state version 2), Stop is not; a state of version 1 opens without bypass.
void TestStopAndBypass() {
    for(const ParamID id : {kParamStop, kParamBypass}) {
        PluginHost host({});
        CHECK(LoadState(*host.processor, MakeState(StartValues(), g_path)) == kResultOk);
        host.Run(4800, 64);
        ParameterChanges changes(4);
        host.external_changes = &changes;
        auto set = [&](double value) {
            int32 index = 0;
            changes.clearQueue();
            changes.addParameterData(id, index)->addPoint(0, value, index);
            host.Process(64);
            changes.clearQueue();
        };
        host.Clear();
        set(1.0);
        host.Run(480, 64); // 5 ms at 48 kHz and more
        const std::int64_t resting_at = host.processor->Adapter().RenderedSamples();
        host.Clear();
        host.Run(4800, 64);
        CHECK(Peak(host.left) == 0.0f && Peak(host.right) == 0.0f);
        CHECK(host.processor->Adapter().RenderedSamples() == resting_at); // the engine rests
        host.Clear();
        set(0.0);
        host.Run(4800, 64);
        CHECK(Peak(host.left) > 0.01f);
        CHECK(std::fabs(host.left[0]) <= 0.01f); // faded in, no step
        CHECK(host.processor->Adapter().RenderedSamples() > resting_at);
        host.external_changes = nullptr;
        CHECK(host.allocations == 0);
    }
    // Saved: Bypass in version 2, not Stop; version 1 still read, without bypass.
    {
        PluginHost host({});
        std::array<double, kSavedParamCount> values = StartValues();
        values[kSavedParamCount - 1] = 1.0; // Bypass
        CHECK(LoadState(*host.processor, MakeState(values, g_path)) == kResultOk);
        host.Run(4800, 64);
        CHECK(Peak(host.left) == 0.0f); // opens bypassed, without a fade-out
        MemoryStream stream;
        CHECK(host.processor->getState(&stream) == kResultOk);
        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        SavedState saved;
        CHECK(ReadSavedState(&stream, saved) && saved.values[kSavedParamCount - 1] == 1.0);
        // A version 1 state: 16 values.
        MemoryStream v1;
        IBStreamer out(&v1, kLittleEndian);
        CHECK(out.writeInt32(1));
        const std::array<double, kSavedParamCount> start = StartValues();
        for(int i = 0; i < kSavedParamCountV1; ++i)
            CHECK(out.writeDouble(start[static_cast<std::size_t>(i)]));
        CHECK(out.writeInt32(static_cast<int32>(g_path.size())));
        CHECK(out.writeRaw(g_path.data(), static_cast<int32>(g_path.size())) == static_cast<TSize>(g_path.size()));
        v1.seek(0, IBStream::kIBSeekSet, nullptr);
        CHECK(host.processor->setState(&v1) == kResultOk);
        host.Clear();
        host.Run(4800, 64);
        CHECK(Peak(host.left) > 0.01f); // not bypassed
    }
    std::printf("stop and bypass: faded out in 5 ms, the engine rests and goes on where it stopped, "
                "faded in; Bypass saved (state 2), version 1 still read\n");
}

// The map takes the mouse (0.1.8): a click or drag sets START to that place in the
// file, the wheel steps SPRAY; without a file a click does nothing.
void TestMapEdits() {
    auto map = VSTGUI::makeOwned<MapView>(VSTGUI::CRect(0, 0, 840, 120));
    std::vector<std::string> calls;
    MapView::Edits edits;
    edits.start_begin = [&] { calls.push_back("begin"); };
    edits.start = [&](double share) { char t[32]; std::snprintf(t, sizeof t, "start %.3f", share); calls.push_back(t); };
    edits.start_end = [&] { calls.push_back("end"); };
    edits.spray = [&](double step) { char t[32]; std::snprintf(t, sizeof t, "spray %+.3f", step); calls.push_back(t); };
    map->SetEdits(std::move(edits));
    CHECK(!map->PressAt(420) && calls.empty()); // no file yet
    EngineDisplay display;
    display.sequence = 1;
    display.frames = 48000;
    display.loop_length = 48000;
    map->Show(display, nullptr);
    CHECK(map->PressAt(420));
    map->DragTo(-30);
    map->DragTo(2000);
    map->DragTo(210);
    map->Release();
    map->Release(); // a second release does nothing
    CHECK(map->Wheel(1.0, false) && map->Wheel(-3.0, true) && !map->Wheel(0.0, false));
    const std::vector<std::string> expected = {"begin", "start 0.500", "start 0.000", "start 1.000", "start 0.250",
                                               "end", "spray +0.025", "spray -0.005"};
    CHECK(calls == expected);
    std::printf("map edits: a click or drag places START in the file (an edit with begin and end), "
                "the wheel steps SPRAY (finer with Shift); nothing without a file\n");
}

// The WINDOW symbol (0.1.8) draws the core's mixed window: Rectangle flat at the top,
// Bartlett a triangle, Gaussian highest in the middle.
void TestWindowSymbol() {
    CHECK(WindowShapeView::Sample(1.0f, 0.1f) == 1.0f && WindowShapeView::Sample(1.0f, 0.9f) == 1.0f);
    CHECK(std::fabs(WindowShapeView::Sample(0.5f, 0.25f) - 0.5f) < 0.02f);
    CHECK(WindowShapeView::Sample(0.0f, 0.5f) > 0.95f && WindowShapeView::Sample(0.0f, 0.05f) < 0.2f);
    std::printf("window symbol: the core's shapes, mixed as the core mixes them\n");
}

void TestViewLayout() {
    constexpr double width = 620.0, height = 68.0;
    int white = 0, black = 0;
    for(int note = kKeyboardLowNote; note <= kKeyboardHighNote; ++note) {
        const KeyRect key = KeyRectFor(note, width, height);
        CHECK(key.left >= 0.0 && key.right <= width + 1e-9 && key.bottom <= height);
        const double centre = (key.left + key.right) / 2.0;
        if(IsBlackKey(note)) {
            ++black;
            CHECK(NoteAt(centre, height * 0.3, width, height) == note);
        } else {
            ++white;
            CHECK(NoteAt(centre, height * 0.9, width, height) == note);
        }
    }
    CHECK(white == kKeyboardWhiteKeys && black == 10);
    CHECK(ClampKeyboardTranspose(-36) == -24 && ClampKeyboardTranspose(13) == 12);
    const auto same = [](EditorSize a, EditorSize b) { return a.width == b.width && a.height == b.height; };
    CHECK(same(EditorSizeFor(1.0, false), {700, 436}) && same(EditorSizeFor(1.0, true), {700, 522}));
    using Clock = DragConstraint::Clock;
    Clock::time_point now{};
    DragConstraint drag;
    now += std::chrono::milliseconds(10);
    CHECK(same(drag.Apply({1050, 436}, {700, 436}, 1.0, false, now), {1050, 654}));
    for(const bool keyboard : {false, true})
        for(int w = 200; w <= 1600; w += 7)
            for(int h = 80; h <= 900; h += 31) {
                const EditorSize size = ConstrainEditorSize({w, h}, EditorSizeFor(1.0, keyboard), 1.0, keyboard, true);
                CHECK(std::abs(size.height - size.width * EditorBaseHeight(keyboard) / kEditorWidth) < 1.0);
                CHECK(size.width >= 350 && size.width <= 1400);
            }
    for(const bool keyboard : {false, true})
        for(const double scale : {1.0, 1.5, 2.0})
            for(const double zoom : {0.5, 1.0, 1.3, 2.0}) {
                const EditorSize size = EditorSizeFor(scale * zoom, keyboard);
                CHECK(std::fabs(EditorZoomForSize(size.width, size.height, scale, keyboard) - zoom) < 0.005);
            }
    std::printf("layout: 25 keys where they are drawn; window 700 x 436 (522 with keys), aspect "
                "ratio and zoom limits kept\n");
}


// --- Review findings MS-R02 to MS-R04 (Codex, 2026-11-17) --------------------

// A host's component handler that puts every edit at sample 0 of the next block,
// keeping one value per parameter and position as the SDK's queue does
// (ParameterValueQueue::addPoint replaces a point at the same offset). Counts
// setDirty() calls.
class CoalescingHandler : public IComponentHandler, public IComponentHandler2 {
  public:
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override {
        int32 index = 0;
        IParamValueQueue* queue = changes.addParameterData(id, index);
        return queue && queue->addPoint(0, value, index) == kResultOk ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }
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

    ParameterChanges changes{8};
    int dirty = 0;
};

// MS-R02: project loads in quick succession, each with another file and another
// Freeze, a 64-sample block after each, while files wait in the retire queue. The
// engine must always run one complete project: the file and the values of the
// same load, never the new values with the old file. In the end the last project
// plays, also as the first block of an export.
void TestProjectBackpressure() {
    const FileImage small = MakeImage(101, 48000, 2, 21), large = MakeImage(202, 48000, 2, 22);
    const std::string small_path = WriteWav("small.wav", small), large_path = WriteWav("large.wav", large);
    PluginHost host({});
    int mismatches = 0, waits = 0;
    std::uint32_t last_frames = 0;
    for(int load = 0; load < 400; ++load) {
        const bool odd = load % 2 != 0;
        std::array<double, kSavedParamCount> values = DefaultValues();
        values[static_cast<std::size_t>(Index(kParamFreeze))] = odd ? 1.0 : 0.0;
        CHECK(LoadState(*host.processor, MakeState(values, odd ? large_path : small_path)) == kResultOk);
        host.Run(64, 64);
        const FileData* file = host.processor->Engine().CurrentFile();
        CHECK(file);
        const bool frozen = host.processor->Engine().Frozen();
        // Each project is a pair: the small file with Freeze off, the large one on.
        if((file->frames == 202) != frozen)
            ++mismatches;
        waits += file->frames == last_frames ? 1 : 0;
        last_frames = file->frames;
    }
    // Once the loader's thread has freed the waiting files, the last project plays.
    std::this_thread::sleep_for(FileLoader::kCollectInterval * 4);
    host.Run(64, 64);
    CHECK(host.processor->Engine().CurrentFile()->frames == 202 && host.processor->Engine().Frozen());
    std::printf("project loads under back-pressure: 400 loads, %d blocks with new values and an old "
                "file (must be 0), %d blocks in which a project waited for room; the last one plays\n",
                mismatches, waits);
    CHECK(mismatches == 0);
    CHECK(host.allocations == 0);
}

// MS-R03, MS-R08: the RESET button. Every press reaches the processor, also with a
// host that keeps one value per parameter and position, when the window closes right
// after the press, and for several presses in a row. The window makes no edit of
// parameter 121 for it (that one stays for automation).
void TestResetButtonThroughHost() {
    IPtr<HostApplication> app = owned(new HostApplication());
    PluginHost host({});
    CHECK(LoadState(*host.processor, MakeState(StartValues(), g_path)) == kResultOk);
    host.Run(4800, 64);
    IPtr<Controller> controller = owned(new Controller());
    CHECK(controller->initialize(app) == kResultOk);
    CoalescingHandler handler;
    CHECK(controller->setComponentHandler(&handler) == kResultOk);
    auto* processor_point = static_cast<IConnectionPoint*>(host.processor.get());
    auto* controller_point = static_cast<IConnectionPoint*>(controller.get());
    CHECK(processor_point->connect(controller_point) == kResultOk);
    CHECK(controller_point->connect(processor_point) == kResultOk);
    host.external_changes = &handler.changes;
    auto block = [&] {
        host.Process(64);
        handler.changes.clearQueue();
    };
    auto resets = [&] { return host.processor->Engine().Resets(); };
    const std::uint32_t start = resets();
    {
        // A press, then the window goes before the next block.
        IPtr<IPlugView> view = owned(controller->createView(ViewType::kEditor));
        auto* editor = dynamic_cast<Editor*>(view.get());
        CHECK(editor);
        editor->PressReset();
        CHECK(handler.changes.getParameterCount() == 0); // no edit of parameter 121
    }
    block();
    CHECK(resets() == start + 1);
    // Three presses with blocks in between: three resets.
    IPtr<IPlugView> view = owned(controller->createView(ViewType::kEditor));
    auto* editor = dynamic_cast<Editor*>(view.get());
    for(int press = 0; press < 3; ++press) {
        editor->PressReset();
        block();
        block();
    }
    CHECK(resets() == start + 4);
    // Several presses before one block restart once: the same restart from START.
    editor->PressReset();
    editor->PressReset();
    block();
    CHECK(resets() == start + 5);
    // A press while no audio runs does not act later.
    editor->PressReset();
    std::this_thread::sleep_for(Processor::kKeyboardNoteLifetime + std::chrono::milliseconds(50));
    block();
    CHECK(resets() == start + 5);
    // MS-R13: a press just before an activation acts after it.
    editor->PressReset();
    host.processor->setProcessing(false);
    CHECK(host.processor->setActive(false) == kResultOk);
    CHECK(host.processor->setActive(true) == kResultOk);
    host.processor->setProcessing(true);
    const std::uint32_t after_restart = resets(); // a fresh engine counts from 0
    block();
    CHECK(resets() == after_restart + 1);
    auto reactivate = [&] {
        host.processor->setProcessing(false);
        CHECK(host.processor->setActive(false) == kResultOk);
        CHECK(host.processor->setActive(true) == kResultOk);
        host.processor->setProcessing(true);
    };
    // MS-R18: a press, a call with no samples, then an activation: it acts after it.
    editor->PressReset();
    host.Process(0);
    reactivate();
    const std::uint32_t after_empty = resets();
    block();
    CHECK(resets() == after_empty + 1);
    // An automated press that the adapter has put on a quantum not rendered yet (a
    // one-sample block in the middle of a quantum), then an activation: it acts too.
    host.Process(1);
    {
        int32 point = 0;
        handler.changes.addParameterData(kParamReset, point)->addPoint(0, 1.0, point);
    }
    host.Process(1);
    handler.changes.clearQueue();
    CHECK(host.processor->Adapter().ControlWaiting(static_cast<int>(kResetControl)));
    reactivate();
    const std::uint32_t after_waiting = resets();
    block();
    CHECK(resets() == after_waiting + 1);
    {
        int32 point = 0; // the button let go again, as a host's automation would
        handler.changes.addParameterData(kParamReset, point)->addPoint(0, 0.0, point);
        block();
    }
    // MS-R22: the same, with a change of sample rate before the activation: the press
    // waits in the adapter, setupProcessing prepares it for 96 kHz, it still acts once.
    for(const double rate : {96000.0, 48000.0}) {
        host.Process(1);
        {
            int32 point = 0;
            handler.changes.addParameterData(kParamReset, point)->addPoint(0, 1.0, point);
        }
        host.Process(1);
        handler.changes.clearQueue();
        CHECK(host.processor->Adapter().ControlWaiting(static_cast<int>(kResetControl)));
        host.processor->setProcessing(false);
        CHECK(host.processor->setActive(false) == kResultOk);
        host.Setup(rate);
        CHECK(host.processor->Adapter().HostRate() == rate);
        CHECK(host.processor->setActive(true) == kResultOk);
        host.processor->setProcessing(true);
        const std::uint32_t after_rate = resets();
        host.Process(1024);
        CHECK(resets() == after_rate + 1);
        int32 point = 0;
        handler.changes.addParameterData(kParamReset, point)->addPoint(0, 0.0, point);
        block();
    }
    // Two presses in two host blocks of one sample each: each acts, unless both fall
    // into one 64-sample quantum the engine has not rendered yet; then they are one
    // restart from START (the same sound; documented). Never none.
    const std::uint32_t before_small = resets();
    editor->PressReset();
    host.Process(1);
    editor->PressReset();
    host.Process(1);
    block();
    const std::uint32_t small_resets = resets() - before_small;
    CHECK(small_resets == 1 || small_resets == 2);
    const std::uint32_t before_automation = resets();
    // Automation of parameter 121 still acts on its rising edge.
    int32 index = 0;
    handler.changes.addParameterData(kParamReset, index)->addPoint(0, 1.0, index);
    block();
    CHECK(resets() == before_automation + 1);
    view = nullptr;
    host.external_changes = nullptr;
    CHECK(processor_point->disconnect(controller_point) == kResultOk);
    CHECK(controller_point->disconnect(processor_point) == kResultOk);
    CHECK(controller->terminate() == kResultOk);
    CHECK(host.allocations == 0);
    std::printf("reset button: every press from the window reaches the processor, also when the "
                "window closes at once, three in a row, and across an activation after a call with "
                "no samples or with the press still waiting in the adapter; automation of "
                "parameter 121 still acts\n");
}

// MS-R07: audio blocks between the moment the project's file is handed over and the
// moment its values are. The engine must go on with the complete old project, never
// the new file with the old values; afterwards the new project plays, complete. Also
// with an activation in that moment.
void TestProjectHandoverGap() {
    const FileImage small = MakeImage(101, 48000, 2, 23), large = MakeImage(202, 48000, 2, 24);
    const std::string small_path = WriteWav("gap-small.wav", small), large_path = WriteWav("gap-large.wav", large);
    for(const bool activate : {false, true}) {
        PluginHost host({});
        std::array<double, kSavedParamCount> a = DefaultValues(), b = DefaultValues();
        b[static_cast<std::size_t>(Index(kParamFreeze))] = 1.0;
        CHECK(LoadState(*host.processor, MakeState(a, small_path)) == kResultOk);
        host.Run(256, 64);
        CHECK(host.processor->Engine().CurrentFile()->frames == 101 && !host.processor->Engine().Frozen());
        int checked = 0;
        host.processor->SetProjectLoadHookForTest([&] {
            if(activate) {
                host.processor->setProcessing(false);
                CHECK(host.processor->setActive(false) == kResultOk);
                CHECK(host.processor->setActive(true) == kResultOk);
                host.processor->setProcessing(true);
            }
            for(int i = 0; i < 20; ++i) {
                host.Process(64);
                const LooperEngine& engine = host.processor->Engine();
                // Still project A, complete: its file and its Freeze.
                CHECK(engine.CurrentFile()->frames == 101 && engine.NextFile()->frames == 101);
                CHECK(!engine.Frozen());
                ++checked;
            }
        });
        CHECK(LoadState(*host.processor, MakeState(b, large_path)) == kResultOk);
        host.processor->SetProjectLoadHookForTest({});
        host.Process(64);
        CHECK(host.processor->Engine().CurrentFile()->frames == 202 && host.processor->Engine().Frozen());
        CHECK(checked == 20 && host.allocations == 0);
    }
    std::printf("project hand-over: blocks between the file and the values of a project, with and "
                "without an activation there, play the complete old project; then the new one\n");
}

// MS-R04, MS-R09: a file chosen in the window marks the project as changed, once per
// file, also with no window open: the processor reports it through a read-only output
// parameter, which the host passes to the controller. A failed choice, status replies
// and loading a project do not.
void TestFileChangeMarksProject() {
    IPtr<HostApplication> app = owned(new HostApplication());
    PluginHost host({});
    IPtr<Controller> controller = owned(new Controller());
    CHECK(controller->initialize(app) == kResultOk);
    CoalescingHandler handler;
    CHECK(controller->setComponentHandler(&handler) == kResultOk);
    auto* processor_point = static_cast<IConnectionPoint*>(host.processor.get());
    auto* controller_point = static_cast<IConnectionPoint*>(controller.get());
    CHECK(processor_point->connect(controller_point) == kResultOk);
    CHECK(controller_point->connect(processor_point) == kResultOk);
    // One block and what a host does with the processor's output parameters: it hands
    // them to the controller (on its UI thread; here, the same thread).
    int reported = 0;
    auto block = [&] {
        host.Process(256);
        ParameterChanges& out = host.OutChanges();
        for(int32 i = 0; i < out.getParameterCount(); ++i) {
            IParamValueQueue* queue = out.getParameterData(i);
            int32 offset = 0;
            ParamValue value = 0.0;
            if(queue && queue->getParameterId() == kParamSampleChanges
               && queue->getPoint(queue->getPointCount() - 1, offset, value) == kResultOk) {
                controller->setParamNormalized(kParamSampleChanges, value);
                ++reported;
            }
        }
    };
    auto settle = [&] {
        CHECK(WaitFor([&] { return host.processor->Loader().Idle(); }));
        block();
        block();
    };
    // No window was ever opened: nothing polls.
    CHECK(controller->LoadFile(g_path));
    settle();
    // The contract since 0.1.7 (MS-R26, MS-R27): every load marks the project, once
    // on each way that reports it (parameter 199, status), so once or twice; nothing
    // else marks it.
    CHECK(reported == 1 && controller->File().path == g_path && handler.dirty >= 1 && handler.dirty <= 2);
    int marked = handler.dirty;
    for(int i = 0; i < 5; ++i) {
        CHECK(controller->RequestFileStatus());
        block();
    }
    CHECK(handler.dirty == marked);
    const std::string bad = Utf8(g_dir / "bad.wav");
    CHECK(controller->LoadFile(bad));
    settle();
    CHECK(handler.dirty == marked && reported == 1);
    // Two files finished before the host delivers anything: marked again.
    CHECK(controller->LoadFile(g_path_mono));
    CHECK(WaitFor([&] { return host.processor->Loader().Idle(); }));
    CHECK(controller->LoadFile(g_path));
    CHECK(WaitFor([&] { return host.processor->Loader().Idle(); }));
    block();
    block();
    CHECK(handler.dirty > marked);
    marked = handler.dirty;
    // MS-R12: the status reply does not come back (the processor's way to the
    // controller is cut); parameter 199 alone marks the project.
    CHECK(processor_point->disconnect(controller_point) == kResultOk);
    CHECK(controller->LoadFile(g_path_mono));
    CHECK(WaitFor([&] { return host.processor->Loader().Idle(); }));
    block();
    block();
    CHECK(handler.dirty == marked + 1);
    marked = handler.dirty;
    CHECK(processor_point->connect(controller_point) == kResultOk);
    CHECK(controller->RequestFileStatus()); // the late status may mark the same load again
    block();
    CHECK(handler.dirty <= marked + 1);
    marked = handler.dirty;
    // A project brings its own file: restoring is no change.
    const std::vector<char> project = MakeState(StartValues(), g_path_mono);
    CHECK(LoadState(*host.processor, project) == kResultOk);
    MemoryStream stream;
    int32 written = 0;
    stream.write(const_cast<char*>(project.data()), static_cast<int32>(project.size()), &written);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    CHECK(controller->setComponentState(&stream) == kResultOk);
    block();
    block();
    CHECK(controller->File().path == g_path_mono && handler.dirty == marked);
    CHECK(processor_point->disconnect(controller_point) == kResultOk);
    CHECK(controller_point->disconnect(processor_point) == kResultOk);
    CHECK(controller->terminate() == kResultOk);
    std::printf("file change: each file from the window marks the project changed, with no "
                "window open, two at a time and without a status reply; polls, a failed load "
                "and a project do not\n");
}

// Parameter 199 from the processor's output, as a host passes it to the controller.
void PassSampleChanges(PluginHost& host, Controller& controller) {
    ParameterChanges& out = host.OutChanges();
    for(int32 i = 0; i < out.getParameterCount(); ++i) {
        IParamValueQueue* queue = out.getParameterData(i);
        int32 offset = 0;
        ParamValue value = 0.0;
        if(queue && queue->getParameterId() == kParamSampleChanges
           && queue->getPoint(queue->getPointCount() - 1, offset, value) == kResultOk)
            controller.setParamNormalized(kParamSampleChanges, value);
    }
}

// MS-R17, MS-R20, MS-R26, MS-R27: a host that keeps the controller and gives it new
// processors, in every order of connect, parameter 199 and status: the status first;
// parameter 199 first with the status late; a value of the old processor delivered
// after the connect (MS-R26); a reconnect to the same processor; no status at all.
// Every load marks the project (once or twice, once on each way), nothing else does;
// the old processor's display is let go.
void TestNewProcessorSameController() {
    IPtr<HostApplication> app = owned(new HostApplication());
    IPtr<Controller> controller = owned(new Controller());
    CHECK(controller->initialize(app) == kResultOk);
    CoalescingHandler handler;
    CHECK(controller->setComponentHandler(&handler) == kResultOk);
    auto* controller_point = static_cast<IConnectionPoint*>(controller.get());
    int before = 0;
    auto marked = [&](const char* when) {
        if(handler.dirty <= before || handler.dirty > before + 2)
            std::printf("FAIL %s: %d setDirty after %d\n", when, handler.dirty, before);
        CHECK(handler.dirty > before && handler.dirty <= before + 2);
        before = handler.dirty;
    };
    auto unmarked = [&](const char* when) {
        if(handler.dirty != before)
            std::printf("FAIL %s: %d setDirty, %d expected\n", when, handler.dirty, before);
        CHECK(handler.dirty == before);
    };
    // The values of parameter 199 a processor put out; passed to the controller now or,
    // as a host may do, later.
    auto outputs = [&](PluginHost& host) {
        std::vector<double> values;
        ParameterChanges& out = host.OutChanges();
        for(int32 i = 0; i < out.getParameterCount(); ++i) {
            IParamValueQueue* queue = out.getParameterData(i);
            int32 offset = 0;
            ParamValue value = 0.0;
            if(queue && queue->getParameterId() == kParamSampleChanges
               && queue->getPoint(queue->getPointCount() - 1, offset, value) == kResultOk)
                values.push_back(value);
        }
        return values;
    };
    auto load = [&](PluginHost& host, const std::string& path, std::vector<double>* held = nullptr) {
        CHECK(controller->LoadFile(path));
        CHECK(WaitFor([&] { return host.processor->Loader().Idle(); }));
        for(int i = 0; i < 2; ++i) {
            host.Process(256);
            for(const double value : outputs(host)) {
                if(held)
                    held->push_back(value);
                else
                    controller->setParamNormalized(kParamSampleChanges, value);
            }
        }
    };
    std::vector<double> late; // A's value, held back by the host until after the connect
    {
        // A: status first. One load; the host holds its value of parameter 199 back (A's
        // count 1, the same count B's first load will have: Codex' case in MS-R26), the
        // status marks it.
        PluginHost a({});
        auto* a_point = static_cast<IConnectionPoint*>(a.processor.get());
        CHECK(a_point->connect(controller_point) == kResultOk);
        CHECK(controller_point->connect(a_point) == kResultOk);
        unmarked("A, connected");
        load(a, g_path, &late);
        CHECK(!late.empty());
        CHECK(controller->RequestFileStatus());
        marked("A, first load, by its status");
        CHECK(controller->RequestDisplay());
        a.Process(256);
        CHECK(controller->RequestDisplay());
        CHECK(controller->Wave() && controller->Display().frames != 0);
        CHECK(a_point->disconnect(controller_point) == kResultOk);
        CHECK(controller_point->disconnect(a_point) == kResultOk);
    }
    PluginHost b({});
    auto* b_point = static_cast<IConnectionPoint*>(b.processor.get());
    {
        // B: parameter 199 first, the status late. Before B loads anything, the host
        // delivers A's held value (MS-R26): it may mark, but must hide nothing.
        CHECK(controller_point->connect(b_point) == kResultOk); // no way back yet
        CHECK(!controller->Wave() && controller->Display().frames == 0);
        for(const double value : late)
            controller->setParamNormalized(kParamSampleChanges, value);
        before = handler.dirty;
        load(b, g_path_mono);
        marked("B, first load after A's late value");
        load(b, g_path);
        marked("B, second load before its status");
        CHECK(b_point->connect(controller_point) == kResultOk);
        CHECK(controller->RequestFileStatus()); // B's first status: may mark its loads again
        CHECK(handler.dirty <= before + 1);
        before = handler.dirty;
        CHECK(controller->RequestFileStatus());
        unmarked("B, a second status");
        load(b, g_path_mono);
        marked("B, a load after its status");
    }
    {
        // B again: the controller reconnects to the same processor.
        CHECK(controller_point->disconnect(b_point) == kResultOk);
        CHECK(controller_point->connect(b_point) == kResultOk);
        unmarked("B, reconnected");
        load(b, g_path);
        marked("B, a load after the reconnect");
        CHECK(controller->RequestFileStatus());
        unmarked("B, status after the reconnect");
        CHECK(b_point->disconnect(controller_point) == kResultOk);
        CHECK(controller_point->disconnect(b_point) == kResultOk);
    }
    {
        // C: never a way back, parameter 199 alone.
        PluginHost c({});
        auto* c_point = static_cast<IConnectionPoint*>(c.processor.get());
        CHECK(controller_point->connect(c_point) == kResultOk);
        for(int i = 0; i < 2; ++i) {
            load(c, i ? g_path : g_path_mono);
            marked("C, parameter 199 alone");
        }
        CHECK(controller_point->disconnect(c_point) == kResultOk);
    }
    CHECK(controller->terminate() == kResultOk);
    std::printf("new processor, same controller: every load marks the project, also after a late "
                "value of the old processor; with the status first, late, after a reconnect and "
                "never; nothing else marks it; the old display is let go\n");
}

// MS-R28, MS-R30, MS-R31: requests of an older connection that reach the processor
// late are refused before they change anything, also before the new connection has
// sent anything, and across a project load; a late key release cannot end a note of
// the newer connection.
IPtr<IMessage> HeldMessage(const char* id, int64 generation) {
    IPtr<IMessage> message = owned(new HostMessage());
    message->setMessageID(id);
    message->getAttributes()->setInt(kAttrGeneration, generation);
    message->getAttributes()->setInt(kAttrIssued, static_cast<int64>(std::chrono::steady_clock::now().time_since_epoch().count()));
    return message;
}

void TestLateRequest() {
    IPtr<HostApplication> app = owned(new HostApplication());
    IPtr<Controller> controller = owned(new Controller());
    CHECK(controller->initialize(app) == kResultOk);
    auto* controller_point = static_cast<IConnectionPoint*>(controller.get());
    PluginHost host({});
    auto* processor_point = static_cast<IConnectionPoint*>(host.processor.get());
    CHECK(processor_point->connect(controller_point) == kResultOk);
    CHECK(controller_point->connect(processor_point) == kResultOk);
    // 1. A load request the host holds back; the controller reconnects and loads
    //    another file: the late request is refused (newer generation heard).
    IPtr<IMessage> held = HeldMessage(kLoadMessageId, controller->Generation());
    held->getAttributes()->setBinary(kAttrPath, g_path.data(), static_cast<uint32>(g_path.size()));
    CHECK(controller_point->disconnect(processor_point) == kResultOk);
    CHECK(controller_point->connect(processor_point) == kResultOk);
    CHECK(controller->LoadFile(g_path_mono));
    CHECK(WaitFor([&] { return host.processor->Loader().Idle(); }));
    host.Run(512, 256);
    CHECK(host.processor->Loader().SoundPath() == g_path_mono);
    CHECK(host.processor->notify(held) == kResultFalse);
    CHECK(WaitFor([&] { return host.processor->Loader().Idle(); }));
    CHECK(host.processor->Loader().SoundPath() == g_path_mono);
    // 2. Codex' case in MS-R30: a held load; both ways disconnected and connected
    //    again, nothing of the new connection has reached the processor yet; the host
    //    restores a project with another file. The late load is refused; the project's
    //    file stays and is what getState saves.
    IPtr<IMessage> held2 = HeldMessage(kLoadMessageId, controller->Generation());
    held2->getAttributes()->setBinary(kAttrPath, g_path.data(), static_cast<uint32>(g_path.size()));
    CHECK(processor_point->disconnect(controller_point) == kResultOk);
    CHECK(controller_point->disconnect(processor_point) == kResultOk);
    CHECK(processor_point->connect(controller_point) == kResultOk);
    CHECK(LoadState(*host.processor, MakeState(StartValues(), g_path_mono)) == kResultOk);
    host.Run(512, 256);
    CHECK(host.processor->notify(held2) == kResultFalse);
    CHECK(WaitFor([&] { return host.processor->Loader().Idle(); }));
    host.Run(512, 256);
    CHECK(host.processor->Loader().SoundPath() == g_path_mono);
    // 3. Without a disconnect: a load made before a project load is overtaken by it;
    //    one made after it is taken.
    CHECK(controller_point->connect(processor_point) == kResultOk);
    IPtr<IMessage> before = HeldMessage(kLoadMessageId, controller->Generation());
    before->getAttributes()->setBinary(kAttrPath, g_path.data(), static_cast<uint32>(g_path.size()));
    CHECK(LoadState(*host.processor, MakeState(StartValues(), g_path_mono)) == kResultOk);
    CHECK(host.processor->notify(before) == kResultFalse);
    CHECK(controller->LoadFile(g_path));
    CHECK(WaitFor([&] { return host.processor->Loader().Idle(); }));
    host.Run(512, 256);
    CHECK(host.processor->Loader().SoundPath() == g_path);
    // 4. MS-R31: key 72 held by the old connection; its release is held back. After the
    //    disconnect (which releases the key) and a reconnect, key 72 is struck again.
    //    The late release is refused and the new note sounds on.
    CHECK(controller->PlayNote(72));
    host.Run(512, 256);
    IPtr<IMessage> release = HeldMessage(kNoteMessageId, controller->Generation());
    release->getAttributes()->setInt(kNoteMessageNote, 72);
    release->getAttributes()->setInt(kNoteMessageVelocity, 0);
    CHECK(processor_point->disconnect(controller_point) == kResultOk);
    CHECK(controller_point->disconnect(processor_point) == kResultOk);
    host.Run(512, 256);
    const float released = host.processor->Engine().EffectivePitch();
    CHECK(processor_point->connect(controller_point) == kResultOk);
    CHECK(controller_point->connect(processor_point) == kResultOk);
    CHECK(controller->PlayNote(72));
    host.Run(512, 256);
    const float held_pitch = host.processor->Engine().EffectivePitch();
    CHECK(held_pitch > released * 1.5f); // an octave up while key 72 is held
    CHECK(host.processor->notify(release) == kResultFalse);
    host.Run(512, 256);
    CHECK(host.processor->Engine().EffectivePitch() > released * 1.5f); // still the new note
    CHECK(controller->ReleaseNote(72));
    host.Run(512, 256);
    // A second controller instance takes a newer generation.
    IPtr<Controller> second = owned(new Controller());
    CHECK(second->initialize(app) == kResultOk);
    CHECK(second->Generation() > controller->Generation());
    CHECK(second->terminate() == kResultOk);
    CHECK(processor_point->disconnect(controller_point) == kResultOk);
    CHECK(controller_point->disconnect(processor_point) == kResultOk);
    CHECK(controller->terminate() == kResultOk);
    std::printf("late requests: refused after a newer generation, after a disconnect before the new "
                "connection said anything, and before a project load; a late key release cannot "
                "end a newer note; a new controller takes a newer generation\n");
}

// MS-R32: the controller goes from processor A to B and back to A. A's status, with
// no new file since, marks nothing.
void TestReturnToProcessor() {
    IPtr<HostApplication> app = owned(new HostApplication());
    IPtr<Controller> controller = owned(new Controller());
    CHECK(controller->initialize(app) == kResultOk);
    CoalescingHandler handler;
    CHECK(controller->setComponentHandler(&handler) == kResultOk);
    auto* controller_point = static_cast<IConnectionPoint*>(controller.get());
    PluginHost a({}), b({});
    auto* a_point = static_cast<IConnectionPoint*>(a.processor.get());
    auto* b_point = static_cast<IConnectionPoint*>(b.processor.get());
    auto attach = [&](IConnectionPoint* p) { CHECK(p->connect(controller_point) == kResultOk); CHECK(controller_point->connect(p) == kResultOk); };
    auto detach = [&](IConnectionPoint* p) { CHECK(p->disconnect(controller_point) == kResultOk); CHECK(controller_point->disconnect(p) == kResultOk); };
    attach(a_point);
    CHECK(controller->LoadFile(g_path));
    CHECK(WaitFor([&] { return a.processor->Loader().Idle(); }));
    a.Run(512, 256);
    CHECK(controller->RequestFileStatus());
    const int after_load = handler.dirty;
    CHECK(after_load >= 1);
    detach(a_point);
    attach(b_point);
    CHECK(handler.dirty == after_load);
    detach(b_point);
    attach(a_point); // its status: still one file, nothing new
    CHECK(controller->RequestFileStatus());
    CHECK(handler.dirty == after_load);
    detach(a_point);
    CHECK(controller->terminate() == kResultOk);
    std::printf("return to a processor: A -> B -> A marks nothing without a new file\n");
}

// MS-R21: display replies on their way when the connection changes, or overtaken by
// a newer one, are not taken: the window never keeps another processor's waveform.
void TestLateDisplayReply() {
    IPtr<HostApplication> app = owned(new HostApplication());
    IPtr<Controller> controller = owned(new Controller());
    CHECK(controller->initialize(app) == kResultOk);
    auto* controller_point = static_cast<IConnectionPoint*>(controller.get());
    PluginHost host({});
    auto* processor_point = static_cast<IConnectionPoint*>(host.processor.get());
    CHECK(processor_point->connect(controller_point) == kResultOk);
    const int64 old_generation = controller->Generation();
    CHECK(controller_point->connect(processor_point) == kResultOk);
    CHECK(controller->Generation() != old_generation);
    CHECK(LoadState(*host.processor, MakeState(StartValues(), g_path)) == kResultOk);
    host.Run(256, 256);
    CHECK(controller->RequestDisplay());
    host.Run(256, 256);
    CHECK(controller->RequestDisplay());
    const EngineDisplay current = controller->Display();
    CHECK(controller->Wave() && current.sequence != 0);
    const WaveOverview kept = *controller->Wave();
    // A reply as the old connection would have sent it: the same sequence number and
    // length, another waveform.
    auto reply = [&](int64 generation, int64 number) {
        IPtr<IMessage> message = owned(new HostMessage());
        message->setMessageID(kDisplayMessageId);
        message->getAttributes()->setInt(kAttrGeneration, generation);
        message->getAttributes()->setInt(kAttrReply, number);
        EngineDisplay frame = current;
        frame.grain_count = 0;
        WaveOverview wave = kept;
        for(std::uint32_t c = 0; c < wave.columns; ++c)
            wave.low[c] = wave.high[c] = 1111;
        message->getAttributes()->setBinary(kAttrFrame, &frame, sizeof frame);
        message->getAttributes()->setBinary(kAttrWave, &wave, sizeof wave);
        return message;
    };
    IPtr<IMessage> late = reply(old_generation, 1000000);
    CHECK(controller->notify(late) == kResultFalse);
    IPtr<IMessage> overtaken = reply(controller->Generation(), 1); // older than the replies taken
    CHECK(controller->notify(overtaken) == kResultFalse);
    for(int i = 0; i < 5; ++i) {
        CHECK(controller->RequestDisplay());
        host.Run(256, 256);
    }
    CHECK(controller->Wave() && controller->Wave()->low[0] == kept.low[0]
          && controller->Wave()->high[0] == kept.high[0]);
    CHECK(processor_point->disconnect(controller_point) == kResultOk);
    CHECK(controller_point->disconnect(processor_point) == kResultOk);
    CHECK(controller->terminate() == kResultOk);
    std::printf("late display replies: one of an earlier connection and one overtaken by a "
                "newer reply are not taken; the waveform stays that of the file the core reads\n");
}

// MS-R16: a valid project file whose decoding runs out of memory. setState refuses
// the project: the old one plays on and is what getState saves.
void TestProjectWithoutMemory() {
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
    PluginHost host({});
    CHECK(LoadState(*host.processor, MakeState(StartValues(), g_path)) == kResultOk);
    host.Run(256, 64);
    const std::uint32_t frames = host.processor->Engine().CurrentFile()->frames;
    CHECK(frames != 0);
    const std::uint64_t failed_before = test_support::g_failed_allocations;
    // The reader's conversion buffer: 1 MiB and its alignment.
    test_support::g_fail_size_from = std::size_t(1) << 20;
    test_support::g_fail_size_to = (std::size_t(1) << 20) + 64;
    const tresult refused = LoadState(*host.processor, MakeState(DefaultValues(), g_path_mono));
    test_support::g_fail_size_to = 0;
    CHECK(test_support::g_failed_allocations > failed_before);
    CHECK(refused == kOutOfMemory);
    host.Run(256, 64);
    CHECK(host.processor->Engine().CurrentFile()->frames == frames);
    CHECK(host.processor->Loader().SoundPath() == g_path);
    MemoryStream stream;
    CHECK(host.processor->getState(&stream) == kResultOk);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    SavedState saved;
    CHECK(ReadSavedState(&stream, saved) && saved.path == g_path);
    // With memory the same project loads.
    CHECK(LoadState(*host.processor, MakeState(DefaultValues(), g_path_mono)) == kResultOk);
    host.Run(256, 64);
    CHECK(host.processor->Loader().SoundPath() == g_path_mono);
    std::printf("project without memory to decode its file: refused, the old project plays on "
                "and is saved\n");
#else
    std::printf("project without memory: Release builds only (Debug STL proxies)\n");
#endif
}

// The window's display (0.1.4), from the engine through the processor and the
// controller to the window: nothing is published until a window asks, the reply
// shows the file the core reads with the overview of that very file, the overview
// travels once per file, and the sound is bit for bit the same with requests on
// every block. Damaged display messages are refused.
void TestDisplayPath() {
    const HostScore score = Performance(48000.0, 3 * 48000, 61);
    PluginHost quiet({}, score), watched({}, score);
    CHECK(LoadState(*quiet.processor, MakeState(StartValues(), g_path)) == kResultOk);
    CHECK(LoadState(*watched.processor, MakeState(StartValues(), g_path)) == kResultOk);
    IPtr<HostApplication> app = owned(new HostApplication());
    IPtr<Controller> controller = owned(new Controller());
    CHECK(controller->initialize(app) == kResultOk);
    auto* processor_point = static_cast<IConnectionPoint*>(watched.processor.get());
    auto* controller_point = static_cast<IConnectionPoint*>(controller.get());
    CHECK(processor_point->connect(controller_point) == kResultOk);
    CHECK(controller_point->connect(processor_point) == kResultOk);

    // Blocks before any request publish nothing: the first reply is empty.
    watched.Run(4800, 256);
    quiet.Run(4800, 256);
    CHECK(controller->RequestDisplay());
    CHECK(controller->Display().frames == 0 && controller->Wave() == nullptr);
    // From now on a request after every block, as an open window's timer would.
    const WaveOverview* first_wave = nullptr;
    int with_wave = 0, requests = 0;
    std::uint64_t most_grains = 0;
    VaryingBlocks blocks;
    while(watched.position < 3 * 48000) {
        const int n = blocks();
        watched.Process(n);
        quiet.Process(n);
        CHECK(controller->RequestDisplay());
        ++requests;
        const EngineDisplay& d = controller->Display();
        CHECK(d.frames == g_image.frames && d.sequence != 0);
        CHECK(d.playhead >= d.loop_start && d.playhead < d.loop_start + d.loop_length);
        most_grains = std::max<std::uint64_t>(most_grains, d.grain_count);
        const WaveOverview* wave = controller->Wave();
        CHECK(wave && wave->sequence == d.sequence && wave->frames == d.frames
              && wave->columns == WaveOverview::kMaxColumns);
        if(!first_wave)
            first_wave = wave;
        with_wave += wave == first_wave ? 1 : 0;
    }
    // The overview came once and stayed the same object: sent only with a new file.
    CHECK(with_wave == requests);
    // The overview is that of the file's samples: the loudest column is as loud as
    // the file's loudest sample.
    int peak = 0, file_peak = 0;
    for(std::uint32_t c = 0; c < first_wave->columns; ++c)
        peak = std::max({peak, -int(first_wave->low[c]), int(first_wave->high[c])});
    for(const std::int16_t x : g_image.pcm)
        file_peak = std::max(file_peak, std::abs(int(x)));
    CHECK(peak == file_peak);
    // Watching changes nothing in the sound and costs the audio thread no allocation.
    CHECK(quiet.Stereo() == watched.Stereo());
    CHECK(watched.allocations == 0 && most_grains > 0);

    // A file from the window: after the swap the display shows the new file with its
    // own overview; before it, the old one.
    const std::uint64_t old_sequence = controller->Display().sequence;
    CHECK(controller->LoadFile(g_path_mono));
    CHECK(WaitFor([&] { return watched.processor->Loader().Idle(); }));
    bool saw_new = false;
    for(int i = 0; i < 40 && !saw_new; ++i) {
        watched.Process(64);
        CHECK(controller->RequestDisplay());
        const EngineDisplay& d = controller->Display();
        if(d.sequence != old_sequence) {
            saw_new = true;
            CHECK(d.sequence > old_sequence && d.frames == g_image_mono.frames);
            CHECK(controller->Wave() && controller->Wave()->frames == g_image_mono.frames);
        } else {
            CHECK(d.frames == g_image.frames && controller->Wave() == first_wave);
        }
    }
    CHECK(saw_new);
    // A window opened later starts from what the controller holds.
    IPtr<IPlugView> view = owned(controller->createView(ViewType::kEditor));
    CHECK(view);
    CHECK(processor_point->disconnect(controller_point) == kResultOk);
    CHECK(controller_point->disconnect(processor_point) == kResultOk);
    view = nullptr;
    CHECK(controller->terminate() == kResultOk);

    // Damaged messages: refused, the controller keeps what it had.
    EngineDisplay good;
    good.sequence = 5;
    good.frames = 1000;
    good.loop_start = 100;
    good.loop_length = 500;
    good.playhead = 200;
    good.grain_count = 1;
    good.grains[0] = {300.0f, 0.5f, 0.25f};
    EngineDisplay read;
    CHECK(ReadDisplayBytes(&good, sizeof good, read) && read.grain_count == 1);
    auto refused = [&](auto damage) {
        EngineDisplay bad = good;
        damage(bad);
        return !ReadDisplayBytes(&bad, sizeof bad, read);
    };
    CHECK(!ReadDisplayBytes(&good, sizeof good - 1, read) && !ReadDisplayBytes(nullptr, sizeof good, read));
    CHECK(refused([](EngineDisplay& d) { d.grain_count = EngineDisplay::kMaxGrains + 1; }));
    CHECK(refused([](EngineDisplay& d) { d.playhead = std::nan(""); }));
    CHECK(refused([](EngineDisplay& d) { d.playhead = 1e9; }));
    CHECK(refused([](EngineDisplay& d) { d.frozen = 2; }));
    CHECK(refused([](EngineDisplay& d) { d.grains[0].pan = 1.5f; }));
    CHECK(refused([](EngineDisplay& d) { d.sequence = 0; }));
    CHECK(refused([](EngineDisplay& d) { d.frames = 0; })); // no file, yet a sequence
    // A long file (above 2^24 frames): positions as floats round to just beyond the
    // last frame, and are taken.
    EngineDisplay long_file = good;
    long_file.frames = 50000003;
    long_file.loop_length = 50000003;
    long_file.spray = static_cast<float>(50000003.0);                 // 50000004
    long_file.grains[0].position = static_cast<float>(50000002.9);   // 50000004
    CHECK(double(long_file.grains[0].position) > long_file.frames);
    CHECK(ReadDisplayBytes(&long_file, sizeof long_file, read));
    auto wave = std::make_unique<WaveOverview>();
    wave->sequence = 5;
    wave->frames = 1000;
    wave->columns = 1000;
    CHECK(ReadOverviewBytes(wave.get(), sizeof(WaveOverview), good) != nullptr);
    wave->sequence = 6;
    CHECK(!ReadOverviewBytes(wave.get(), sizeof(WaveOverview), good)); // another file's
    wave->sequence = 5;
    wave->columns = 1001;
    CHECK(!ReadOverviewBytes(wave.get(), sizeof(WaveOverview), good));
    wave->columns = 1000;
    wave->low[3] = 10;
    wave->high[3] = 9;
    CHECK(!ReadOverviewBytes(wave.get(), sizeof(WaveOverview), good));
    CHECK(!ReadOverviewBytes(wave.get(), sizeof(WaveOverview) - 2, good));
    std::printf("display path: nothing published before a window asks; %d replies show the file the "
                "core reads with its own overview, sent once; up to %llu grains; the sound "
                "bit-identical with and without, no allocation in process; the new file after "
                "the swap; damaged messages refused\n",
                requests, static_cast<unsigned long long>(most_grains));
}

// MS-R01, MS-R06: no host call lets an exception out. Every allocation the call
// makes in this thread fails in turn, the first, the second, and so on, until the call
// gets through. Release builds only: in Debug builds the standard library allocates a
// container proxy in noexcept constructors and terminates when that fails (see the
// loader test).
void TestNoExceptionLeavesHostCalls() {
#if !defined(_ITERATOR_DEBUG_LEVEL) || _ITERATOR_DEBUG_LEVEL == 0
    IPtr<HostApplication> app = owned(new HostApplication());
    PluginHost host({});
    IPtr<Controller> controller = owned(new Controller());
    CHECK(controller->initialize(app) == kResultOk);
    auto* processor_point = static_cast<IConnectionPoint*>(host.processor.get());
    auto* controller_point = static_cast<IConnectionPoint*>(controller.get());
    CHECK(processor_point->connect(controller_point) == kResultOk);
    CHECK(controller_point->connect(processor_point) == kResultOk);
    // An editor object registered with the controller, without a window, as a host may
    // have it between attach and open.
    IPtr<IPlugView> view = owned(controller->createView(ViewType::kEditor));
    auto* editor = dynamic_cast<Editor*>(view.get());
    CHECK(editor);
    controller->editorAttached(editor);
    const std::vector<char> project = MakeState(StartValues(), g_path);
    struct Call {
        const char* name;
        std::function<tresult()> run;
    };
    IPtr<IMessage> status = owned(new HostMessage());
    status->setMessageID(kStatusMessageId);
    for(const char* id : {kAttrPath, kAttrInfo, kAttrRequest, kAttrDetail})
        status->getAttributes()->setBinary(id, g_path.data(), static_cast<uint32>(g_path.size()));
    status->getAttributes()->setInt(kAttrState, 1);
    status->getAttributes()->setInt(kAttrSerial, 7);
    status->getAttributes()->setInt(kAttrUserLoads, 0);
    status->getAttributes()->setInt(kAttrProcessor, 12345);
    status->getAttributes()->setInt(kAttrGeneration, controller->Generation());
    IPtr<IMessage> load = owned(new HostMessage());
    load->setMessageID(kLoadMessageId);
    load->getAttributes()->setBinary(kAttrPath, g_path.data(), static_cast<uint32>(g_path.size()));
    load->getAttributes()->setInt(kAttrGeneration, controller->Generation());
    // The display (0.1.4): a request that the processor answers with a frame and the
    // overview of the file playing (the controller claims to hold none), and a reply
    // as the controller receives it.
    CHECK(LoadState(*host.processor, project) == kResultOk);
    host.Run(256, 256);
    IPtr<IMessage> display_request = owned(new HostMessage());
    display_request->setMessageID(kDisplayRequestId);
    display_request->getAttributes()->setInt(kAttrGeneration, controller->Generation());
    display_request->getAttributes()->setInt(kAttrHaveWave, 0);
    CHECK(host.processor->notify(display_request) == kResultOk);
    host.Run(256, 256);
    EngineDisplay frame;
    frame.sequence = 999;
    frame.frames = 1000;
    frame.loop_length = 1000;
    auto wave = std::make_unique<WaveOverview>();
    wave->sequence = 999;
    wave->frames = 1000;
    wave->columns = 1000;
    IPtr<IMessage> display_reply = owned(new HostMessage());
    display_reply->setMessageID(kDisplayMessageId);
    display_reply->getAttributes()->setInt(kAttrGeneration, controller->Generation());
    display_reply->getAttributes()->setInt(kAttrReply, 1000000);
    display_reply->getAttributes()->setBinary(kAttrFrame, &frame, sizeof frame);
    display_reply->getAttributes()->setBinary(kAttrWave, wave.get(), sizeof(WaveOverview));
    const std::vector<Call> calls = {
        {"controller setComponentState", [&] {
             MemoryStream stream;
             int32 written = 0;
             stream.write(const_cast<char*>(project.data()), static_cast<int32>(project.size()), &written);
             stream.seek(0, IBStream::kIBSeekSet, nullptr);
             return controller->setComponentState(&stream);
         }},
        {"controller setParamNormalized", [&] { return controller->setParamNormalized(kParamDensity, 0.7); }},
        {"controller notify (status)", [&] { return controller->notify(status); }},
        {"processor setState", [&] { return LoadState(*host.processor, project); }},
        {"processor getState", [&] {
             MemoryStream stream;
             return host.processor->getState(&stream);
         }},
        {"processor notify (load)", [&] { return host.processor->notify(load); }},
        {"controller getParamStringByValue", [&] {
             String128 text{};
             tresult result = kResultOk;
             for(const ParamID id : kParamIds) {
                 const tresult one = controller->getParamStringByValue(id, 0.37, text);
                 if(one != kResultOk)
                     result = one;
             }
             return result;
         }},
        {"controller getParamValueByString", [&] {
             ParamValue value = 0.0;
             tresult result = kResultOk;
             for(const ParamID id : kParamIds) {
                 // Longer than std::string's own buffer, so that converting it allocates.
                 TChar text[] = {' ', ' ', ' ', ' ', ' ', ' ', '2', '5', '.', '5', '0', '0', '0',
                                 '0', '0', '0', '0', '0', '0', '0', 0};
                 const tresult one = controller->getParamValueByString(id, text, value);
                 if(one != kResultOk && one != kResultFalse)
                     result = one;
             }
             return result;
         }},
        {"controller createView", [&] {
             IPtr<IPlugView> made = owned(controller->createView(ViewType::kEditor));
             return made ? kResultOk : kResultFalse;
         }},
        {"processor notify (display request)", [&] { return host.processor->notify(display_request); }},
        {"controller notify (display)", [&] { return controller->notify(display_reply); }},
        // MS-R23: a reply whose attribute list the host makes on first access.
        {"controller notify (display, attributes made on access)", [&] {
             test_support::g_fail_armed = false; // the message itself is the host's
             IPtr<IMessage> bare = owned(new HostMessage());
             bare->setMessageID(kDisplayMessageId);
             test_support::g_fail_armed = true;
             return controller->notify(bare);
         }},
        {"processor notify (note, attributes made on access)", [&] {
             test_support::g_fail_armed = false;
             IPtr<IMessage> bare = owned(new HostMessage());
             bare->setMessageID(kNoteMessageId);
             test_support::g_fail_armed = true;
             return host.processor->notify(bare);
         }},
        // MS-R19: what the factory calls, and the processor's life cycle: initialize,
        // an activation at another rate than the adapter's (44.1 kHz, the SDK's default
        // setup, against the 48 kHz of initialize), setupProcessing to 96 kHz.
        {"factory: processor, initialize, setActive, setupProcessing", [&] {
             FUnknown* made = CreateProcessorInstance(nullptr);
             if(!made)
                 return static_cast<tresult>(kOutOfMemory);
             IPtr<Processor> made_processor = owned(static_cast<Processor*>(static_cast<IAudioProcessor*>(made)));
             tresult result = made_processor->initialize(app);
             if(result == kResultOk) {
                 result = made_processor->setActive(true);
                 if(result == kResultOk) {
                     CHECK(made_processor->Adapter().HostRate() == 44100.0);
                     made_processor->setActive(false);
                     ProcessSetup setup{kRealtime, kSample32, 512, 96000.0};
                     result = made_processor->setupProcessing(setup);
                     if(result == kResultOk) {
                         CHECK(made_processor->Adapter().HostRate() == 96000.0);
                         result = made_processor->setActive(true);
                         made_processor->setActive(false);
                     } else {
                         CHECK(made_processor->Adapter().HostRate() == 44100.0); // as it was
                     }
                 } else {
                     CHECK(made_processor->Adapter().HostRate() == 48000.0); // as it was
                 }
                 made_processor->terminate();
             }
             return result;
         }},
        {"factory: controller, initialize", [&] {
             FUnknown* made = CreateControllerInstance(nullptr);
             if(!made)
                 return static_cast<tresult>(kOutOfMemory);
             IPtr<Controller> made_controller = owned(static_cast<Controller*>(static_cast<IEditController*>(made)));
             const tresult result = made_controller->initialize(app);
             made_controller->terminate();
             return result;
         }},
        {"controller connect", [&] {
             controller_point->disconnect(processor_point);
             return controller_point->connect(processor_point);
         }},
    };
    for(const Call& call : calls) {
        int failures = 0;
        bool through = false;
        for(long long after = 0; after < 2000 && !through; ++after) {
            test_support::g_failed_allocations = 0;
            test_support::g_fail_after = after;
            test_support::g_fail_armed = true;
            tresult result = kResultFalse;
            try {
                result = call.run();
            } catch(...) {
                test_support::g_fail_armed = false;
                std::printf("FAIL %s: an exception left the call after %lld allocations\n", call.name, after);
                std::exit(1);
            }
            test_support::g_fail_armed = false;
            through = test_support::g_failed_allocations == 0;
            if(!through) {
                ++failures;
                CHECK(result == kOutOfMemory || result == kResultOk || result == kResultFalse);
            }
        }
        CHECK(through);
        std::printf("  %s: failed at each of its %d allocations in turn without an exception\n",
                    call.name, failures);
        CHECK(WaitFor([&] { return host.processor->Loader().Idle(); }));
        host.Run(256, 256);
    }
    controller->editorRemoved(editor);
    view = nullptr;
    CHECK(processor_point->disconnect(controller_point) == kResultOk);
    CHECK(controller_point->disconnect(processor_point) == kResultOk);
    CHECK(controller->terminate() == kResultOk);
    std::printf("host calls: no exception leaves them, whichever allocation fails (sixteen calls, "
                "parameter texts, the window, the display, the factory and the life cycle included)\n");
#else
    std::printf("host calls under failing allocations: Release builds only (Debug STL proxies)\n");
#endif
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    g_dir = fs::temp_directory_path() / ("messier-plugin-test-" + std::to_string(_getpid()));
    fs::remove_all(g_dir);
    fs::create_directories(g_dir);
    g_image = MakeImage(3 * 48000, 48000, 2, 1);
    g_image_mono = MakeImage(2 * 44100 + 17, 44100, 1, 2);
    g_path = WriteWav("loop.wav", g_image);
    g_path_mono = WriteWav("mono 44k.wav", g_image_mono);
    TestBuses();
    TestController();
    TestViewLayout();
    TestFileDrop();
    TestKeyboardLatch();
    TestWindowSymbol();
    TestMapEdits();
    TestStopAndBypass();
    TestState();
    TestExportStart();
    TestMessagesAndKeyboard();
    TestLoadWhilePlaying();
    TestProjectBackpressure();
    TestResetButtonThroughHost();
    TestProjectHandoverGap();
    TestFileChangeMarksProject();
    TestDisplayPath();
    TestNoExceptionLeavesHostCalls();
    TestNewProcessorSameController();
    TestLateDisplayReply();
    TestLateRequest();
    TestReturnToProcessor();
    TestProjectWithoutMemory();
    TestActivation();
    TestTwoInstances();
    TestSampleRates();
    TestBlockSizes();
    TestAgainstFirmware();
    fs::remove_all(g_dir);
    std::printf("PASS messier.plugin\n");
    return 0;
}
