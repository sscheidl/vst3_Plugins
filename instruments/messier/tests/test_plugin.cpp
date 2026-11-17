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
#include "editor.h"
#include "parameters.h"
#include "processor.h"
#include "reference_api.h"
#include "state_io.h"
#include "value_display.h"
#include "view_layout.h"
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
    : config_(config), score_(std::move(score)), events_(4096), out_events_(4096), changes_(64) {
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

    IPtr<Processor> processor;
    std::vector<float> left, right;
    std::int64_t position = 0;
    std::uint64_t allocations = 0;

  private:
    IPtr<HostApplication> host_app_;
    HostConfig config_;
    HostScore score_;
    std::size_t next_ = 0;
    HostProcessData data_;
    EventList events_, out_events_;
    ParameterChanges changes_;
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
    for(int i = 0; i < 8; ++i)
        primary[i] = static_cast<float>(start[static_cast<std::size_t>(i)]);
    const int secondary_index[6] = {0, 2, 3, 4, 5, 7};
    for(int i = 0; i < 6; ++i)
        secondary[secondary_index[i]] = static_cast<float>(start[static_cast<std::size_t>(8 + i)]);
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
                const float value = static_cast<float>(ev.value);
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
    const int32 version2 = 2, minus = -1, huge = kMaxPathBytes + 1;
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
            CHECK(Text(*controller, info.id, back) == shown);
        }
    }
    CHECK(Text(*controller, kParamSpeed, 0.5) == "+1.00");
    CHECK(Text(*controller, kParamPitch, 0.5) == "+0.00");
    CHECK(Text(*controller, kParamPitch, 0.0) == "-3.00" && Text(*controller, kParamPitch, 1.0) == "+2.00");
    CHECK(Text(*controller, kParamWindow, 0.0) == "Gaussian" && Text(*controller, kParamWindow, 1.0) == "Rectangle");
    CHECK(Text(*controller, kParamWindow, 0.5) == "Bartlett");
    CHECK(Text(*controller, kParamSize, 0.5) == "25.0");
    CHECK(Text(*controller, kParamSpray, 0.5) == "\xC2\xB1" "25.0");
    CHECK(Text(*controller, kParamFreeze, 1.0) == "On");
    double v = -1;
    CHECK(ParseValue(kParamSpeed, "1x", v) && v == 0.5);
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
    CHECK(same(EditorSizeFor(1.0, false), {700, 306}) && same(EditorSizeFor(1.0, true), {700, 392}));
    using Clock = DragConstraint::Clock;
    Clock::time_point now{};
    DragConstraint drag;
    now += std::chrono::milliseconds(10);
    CHECK(same(drag.Apply({1050, 306}, {700, 306}, 1.0, false, now), {1050, 459}));
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
    std::printf("layout: 25 keys where they are drawn; window 700 x 306 (392 with keys), aspect "
                "ratio and zoom limits kept\n");
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
    TestState();
    TestExportStart();
    TestMessagesAndKeyboard();
    TestLoadWhilePlaying();
    TestActivation();
    TestTwoInstances();
    TestSampleRates();
    TestBlockSizes();
    TestAgainstFirmware();
    fs::remove_all(g_dir);
    std::printf("PASS messier.plugin\n");
    return 0;
}
