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
#include "dialog_start.h"
#include "display_data.h"
#include "editor.h"
#include "fisheye.h"
#include "file_drop.h"
#include "gui_layout.h"
#include "gui_skin.h"
#include "map_view.h"
#include "vstgui/lib/events.h"
#include "message_text.h"
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
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/platform/win32/win32factory.h"
#include "vstgui/lib/vstguiinit.h"
#include "pluginterfaces/base/funknownimpl.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include <windows.h>
#include <objbase.h>
#undef small // rpcndr.h; the tests name files so
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
#include <limits>
#include <sstream>
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

// A host like Studio One (0.2.7): its attribute lists keep no binary attribute of
// length 0. Everything else as the SDK's host classes.
class EmptyDroppingAttributes : public U::Implements<U::Directly<IAttributeList>> {
  public:
    tresult PLUGIN_API setInt(AttrID id, int64 value) override { return inner_->setInt(id, value); }
    tresult PLUGIN_API getInt(AttrID id, int64& value) override { return inner_->getInt(id, value); }
    tresult PLUGIN_API setFloat(AttrID id, double value) override { return inner_->setFloat(id, value); }
    tresult PLUGIN_API getFloat(AttrID id, double& value) override { return inner_->getFloat(id, value); }
    tresult PLUGIN_API setString(AttrID id, const TChar* string) override { return inner_->setString(id, string); }
    tresult PLUGIN_API getString(AttrID id, TChar* string, uint32 size) override {
        return inner_->getString(id, string, size);
    }
    tresult PLUGIN_API setBinary(AttrID id, const void* data, uint32 size) override {
        if(size == 0) {
            ++dropped;
            return kResultFalse;
        }
        if(refused && id && std::strcmp(id, refused) == 0) // 0.2.8, MS-R58: a text the host does not take
            return kResultFalse;
        return inner_->setBinary(id, data, size);
    }
    tresult PLUGIN_API getBinary(AttrID id, const void*& data, uint32& size) override {
        return inner_->getBinary(id, data, size);
    }
    static inline int dropped = 0;
    static inline const char* refused = nullptr;

  private:
    IPtr<IAttributeList> inner_ = HostAttributeList::make();
};

class EmptyDroppingMessage : public U::Implements<U::Directly<IMessage>> {
  public:
    FIDString PLUGIN_API getMessageID() override { return id_.c_str(); }
    void PLUGIN_API setMessageID(FIDString id) override { id_ = id ? id : ""; }
    IAttributeList* PLUGIN_API getAttributes() override { return attributes_; }

  private:
    std::string id_;
    IPtr<IAttributeList> attributes_ = owned(new EmptyDroppingAttributes());
};

class EmptyDroppingHost : public HostApplication {
  public:
    tresult PLUGIN_API createInstance(TUID cid, TUID iid, void** obj) override {
        if(FUnknownPrivate::iidEqual(cid, IMessage::iid) && FUnknownPrivate::iidEqual(iid, IMessage::iid)) {
            *obj = static_cast<IMessage*>(new EmptyDroppingMessage());
            return kResultTrue;
        }
        return HostApplication::createInstance(cid, iid, obj);
    }
};

struct HostConfig {
    double rate = 48000.0;
    int max_block = 2048;
    bool double_precision = false;
    bool drops_empty_binaries = false; // 0.2.7: the host's messages as Studio One's
};

class PluginHost {
  public:
    explicit PluginHost(const HostConfig& config, HostScore score = {})
    : config_(config), score_(std::move(score)), events_(4096), out_events_(4096), changes_(64), out_changes_(8) {
        host_app_ = config.drops_empty_binaries ? IPtr<HostApplication>(owned(new EmptyDroppingHost()))
                                                : owned(new HostApplication());
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
// 0.2.7: in Studio One the name beside LOAD stayed at the project's file after another
// load. Its messages keep no empty binary attribute; the status's empty texts (the
// detail, before any load all four) were missing, and the controller refused every
// status. A host like it: the first status, two loads with distinct names and a failed
// one all arrive.
void TestHostWithoutEmptyTexts() {
    HostConfig config;
    config.drops_empty_binaries = true;
    PluginHost host(config);
    IPtr<HostApplication> app = owned(new EmptyDroppingHost());
    IPtr<Controller> controller = owned(new Controller());
    CHECK(controller->initialize(app) == kResultOk);
    auto* processor_point = static_cast<IConnectionPoint*>(host.processor.get());
    auto* controller_point = static_cast<IConnectionPoint*>(controller.get());
    CHECK(processor_point->connect(controller_point) == kResultOk);
    CHECK(controller_point->connect(processor_point) == kResultOk);
    EmptyDroppingAttributes::dropped = 0;
    CHECK(controller->RequestFileStatus());
    CHECK(controller->File().serial != 0 && controller->File().path.empty()
          && controller->File().sound == SoundState::kNone);
    const std::string other = WriteWav("Another Name, Take 2.wav", g_image);
    for(const std::string& path : {g_path, other}) {
        CHECK(controller->LoadFile(path));
        CHECK(WaitFor([&] { return host.processor->Loader().Idle(); }));
        CHECK(controller->RequestFileStatus());
        CHECK(controller->File().path == path && controller->File().sound == SoundState::kReady
              && controller->File().request_state == RequestState::kNone && controller->File().detail.empty()
              && controller->File().info == "48 kHz stereo, 16 bit, 0:03.0");
    }
    const std::string missing = (std::filesystem::path(g_path).parent_path() / "not there.wav").string();
    CHECK(controller->LoadFile(missing));
    CHECK(WaitFor([&] { return host.processor->Loader().Idle(); }));
    CHECK(controller->RequestFileStatus());
    CHECK(controller->File().path == other && controller->File().request_state == RequestState::kFailed
          && controller->File().request == missing && !controller->File().detail.empty());
    CHECK(EmptyDroppingAttributes::dropped == 0); // no empty text is sent any more
    // MS-R58: a text the host does not take (here the format line) means no status at
    // all, not one that reads as if the text were empty.
    EmptyDroppingAttributes::refused = kAttrInfo;
    CHECK(controller->LoadFile(g_path));
    CHECK(WaitFor([&] { return host.processor->Loader().Idle(); }));
    CHECK(controller->RequestFileStatus());
    CHECK(controller->File().path == other && controller->File().info == "48 kHz stereo, 16 bit, 0:03.0");
    EmptyDroppingAttributes::refused = nullptr;
    CHECK(controller->RequestFileStatus());
    CHECK(controller->File().path == g_path && controller->File().request_state == RequestState::kNone);
    CHECK(processor_point->disconnect(controller_point) == kResultOk);
    CHECK(controller_point->disconnect(processor_point) == kResultOk);
    CHECK(controller->terminate() == kResultOk);
    std::printf("a host without empty binary attributes: the first status, two loads and a failed "
                "one reach the controller\n");
}

// 0.2.8, MS-R58, MS-R59: a status the controller takes names the texts its states need
// and carries a serial in the 32-bit range. One that lost its path, with an unknown
// state, or with a serial out of range is refused, and a later regular one is taken.
void TestStatusChecks() {
    IPtr<HostApplication> app = owned(new HostApplication());
    PluginHost host({});
    IPtr<Controller> controller = owned(new Controller());
    CHECK(controller->initialize(app) == kResultOk);
    auto* processor_point = static_cast<IConnectionPoint*>(host.processor.get());
    auto* controller_point = static_cast<IConnectionPoint*>(controller.get());
    CHECK(processor_point->connect(controller_point) == kResultOk);
    CHECK(controller_point->connect(processor_point) == kResultOk);
    auto status = [&](int64 code, int64 serial, const std::string& path, const std::string& request) {
        IPtr<IMessage> message = owned(new HostMessage());
        message->setMessageID(kStatusMessageId);
        IAttributeList* a = message->getAttributes();
        SetText(a, kAttrPath, path);
        SetText(a, kAttrInfo, path.empty() ? std::string() : std::string("48 kHz"));
        SetText(a, kAttrRequest, request);
        a->setInt(kAttrState, code);
        a->setInt(kAttrSerial, serial);
        a->setInt(kAttrUserLoads, 0);
        a->setInt(kAttrProcessor, 77);
        a->setInt(kAttrGeneration, controller->Generation());
        return controller->notify(message);
    };
    const int64 ready = static_cast<int>(SoundState::kReady), loading = static_cast<int>(RequestState::kLoading) << 4;
    CHECK(status(ready, 1000, "", "") == kInvalidArgument);                        // ready without its file
    CHECK(status(loading, 1000, "", "") == kInvalidArgument);                      // loading without the file asked for
    CHECK(status(static_cast<int>(SoundState::kNone), 1000, g_path, "") == kInvalidArgument); // none with a file
    CHECK(status(6, 1000, g_path, "") == kInvalidArgument);                        // no such sound state
    CHECK(status(3 << 4, 1000, "", g_path) == kInvalidArgument);                   // no such request state
    CHECK(status(ready, -1, g_path, "") == kInvalidArgument);                      // serial below the range
    CHECK(status(ready, 0x100000000ll, g_path, "") == kInvalidArgument);           // and above
    CHECK(controller->File().path != g_path || controller->File().serial < 1000);
    CHECK(status(ready | loading, 1000, g_path, g_path) == kResultOk);
    CHECK(controller->File().path == g_path && controller->File().serial == 1000);
    CHECK(processor_point->disconnect(controller_point) == kResultOk);
    CHECK(controller_point->disconnect(processor_point) == kResultOk);
    CHECK(controller->terminate() == kResultOk);
    std::printf("status checks: a status without the path its state needs, with an unknown state or a serial "
                "out of range is refused; a regular one after it is taken\n");
}

// F6 of the Fable audit (0.2.9): a read asked to give up does so, at the first chunk
// header or within the next MiB of audio, and a read that is not asked loads.
void TestCancelledRead() {
    int asked = 0;
    const WavCancel always{[](const void*) { return true; }, nullptr};
    CHECK(ReadWavFile(g_path, kMaxPcmBytes, always).error == WavError::kCancelled);
    struct Later {
        int* asked;
        int after;
    } const later{&asked, 3};
    const WavCancel third{[](const void* context) {
                              const auto& l = *static_cast<const Later*>(context);
                              return ++*l.asked >= l.after;
                          },
                          &later};
    const WavResult stopped = ReadWavFile(g_path, kMaxPcmBytes, third);
    CHECK(stopped.error == WavError::kCancelled && !stopped.file && asked == 3);
    CHECK(ReadWavFile(g_path, kMaxPcmBytes, WavCancel{}).error == WavError::kNone);
    std::printf("cancelled read: a read asked to give up stops at the next chunk or MiB\n");
}

// F2 of the Fable audit (0.2.9): a file loaded while STOP rests the output is installed at
// once, unheard; the display shows it, and nothing of the old file waits for the sound.
void TestSwapWhileResting() {
    const HostScore score = {{0, Ev::kParam, static_cast<int>(kParamStop), 1.0}};
    PluginHost host({}, score);
    CHECK(LoadState(*host.processor, MakeState(StartValues(), g_path)) == kResultOk);
    host.Run(4800, 256);
    const FileData* before = host.processor->Engine().CurrentFile();
    CHECK(before && before->channels == 2);
    host.processor->Loader().Request(g_path_mono);
    CHECK(WaitFor([&] { return host.processor->Loader().Idle(); }));
    const std::size_t heard = host.left.size();
    host.Run(512, 256);
    CHECK(!host.processor->Engine().Swapping());
    CHECK(host.processor->Engine().CurrentFile() && host.processor->Engine().CurrentFile()->channels == 1);
    EngineDisplay display;
    host.processor->Engine().Display(display);
    CHECK(display.frames == host.processor->Engine().CurrentFile()->frames && display.swapping == 0);
    for(std::size_t i = heard; i < host.left.size(); ++i)
        CHECK(host.left[i] == 0.0f && host.right[i] == 0.0f);
    std::printf("swap while resting: a file loaded during STOP is installed at once, unheard\n");
}

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
    // Beta 2 (the user's decision): PITCH in semitones, also typed.
    CHECK(Text(*controller, kParamPitch, 0.5) == "+0.0");
    CHECK(Text(*controller, kParamPitch, 0.0) == "-36.0" && Text(*controller, kParamPitch, 1.0) == "+24.0");
    CHECK(Text(*controller, kParamWindow, 0.0) == "Gaussian" && Text(*controller, kParamWindow, 1.0) == "Rectangle");
    CHECK(Text(*controller, kParamWindow, 0.5) == "Bartlett");
    CHECK(Text(*controller, kParamSize, 0.5) == "25.0");
    CHECK(Text(*controller, kParamSpray, 0.5) == "\xC2\xB1" "25.0");
    CHECK(Text(*controller, kParamFreeze, 1.0) == "On");
    double v = -1;
    CHECK(ParseValue(kParamSpeed, "1x", v) && std::fabs(v - 7.0 / 12.0) < 1e-12);
    CHECK(ParseValue(kParamSpeed, "0", v) && v == 0.5);
    CHECK(ParseValue(kParamPitchRandom, "+-6 st", v) && v == 0.5);
    CHECK(ParseValue(kParamPitch, "-7.5 st", v) && std::abs(PitchOctaves(v) * 12.0 + 7.5) < 1e-6);
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

// MS-R35 (0.1.10): Bypass and Stop act on their own sample, and the output is the same
// however the host divides its blocks (Codex' case: a point at sample 512 of a block of
// 1024). A switch on and off within one block does not vanish. While the engine rests,
// a strike is dropped; a release and a parameter point act when it goes on.
struct SwitchRun {
    std::vector<float> out;
    std::int64_t rendered = 0;
    float pitch = 0.0f;
};

SwitchRun RunSwitches(const HostScore& score, std::int64_t total, const std::function<int()>& blocks) {
    PluginHost host({}, score);
    CHECK(LoadState(*host.processor, MakeState(StartValues(), g_path)) == kResultOk);
    host.Run(total, blocks);
    CHECK(host.allocations == 0);
    return {host.Stereo(), host.processor->Adapter().RenderedSamples(), host.processor->Engine().EffectivePitch()};
}

void TestSwitchesOnTheSample() {
    for(const ParamID id : {kParamStop, kParamBypass}) {
        const std::int64_t on = 4800 + 512 + 37; // on no block or quantum boundary
        const std::int64_t off = on + 3000;
        const std::int64_t total = off + 6000;
        auto make = [&](bool dropped_note, bool fading_note, bool blip) {
            HostScore score = {{on - 2000, Ev::kNoteOn, 72, 0.8, 5},
                               {on, Ev::kParam, static_cast<int>(id), 1.0}};
            if(fading_note) // 100 samples into the 240-sample fade: still played
                score.push_back({on + 100, Ev::kNoteOn, 67, 0.8, 6});
            if(dropped_note) // the engine rests: dropped
                score.push_back({on + 1000, Ev::kNoteOn, 60, 0.9, 3});
            score.push_back({on + 1200, Ev::kParam, static_cast<int>(kParamDensity), 0.8});
            score.push_back({on + 1500, Ev::kNoteOff, 72, 0.0, 5});
            score.push_back({off, Ev::kParam, static_cast<int>(id), 0.0});
            if(blip) { // on and off again 50 samples later, inside one block of 2048
                score.push_back({off + 2100, Ev::kParam, static_cast<int>(id), 1.0});
                score.push_back({off + 2150, Ev::kParam, static_cast<int>(id), 0.0});
            }
            std::stable_sort(score.begin(), score.end(),
                             [](const Ev& a, const Ev& b) { return a.sample < b.sample; });
            return score;
        };
        const HostScore score = make(true, true, true);
        const SwitchRun reference = RunSwitches(score, total, [] { return 512; });
        const std::vector<std::function<int()>> divisions = {
            [] { return 2048; }, [] { return 1024; }, [] { return 1; }, VaryingBlocks{},
            [n = 0]() mutable { return (n++ % 2) ? 333 : 1000; }};
        int index = 0;
        for(const auto& blocks : divisions) {
            const std::function<int()> next = blocks;
            if(index++ == 2) { // single samples: only around the switches, else too slow
                PluginHost host({}, score);
                CHECK(LoadState(*host.processor, MakeState(StartValues(), g_path)) == kResultOk);
                host.Run(on - 300, 512);
                host.Run(total - (on - 300), next);
                CHECK(host.Stereo() == reference.out);
                continue;
            }
            const SwitchRun run = RunSwitches(score, total, next);
            CHECK(run.out == reference.out);
            CHECK(run.rendered == reference.rendered);
        }
        // The fade starts at the point itself: before it, the output is that of a run
        // without any switch; right after it, it is quieter.
        HostScore plain;
        for(const Ev& ev : score)
            if(ev.a != static_cast<int>(id))
                plain.push_back(ev);
        const SwitchRun unswitched = RunSwitches(plain, total, [] { return 512; });
        const auto n = static_cast<std::size_t>(total);
        for(std::size_t i = 0; i < static_cast<std::size_t>(on); ++i)
            CHECK(reference.out[i] == unswitched.out[i] && reference.out[n + i] == unswitched.out[n + i]);
        // Resting from the fade's end until `off`: silent, and the engine stood.
        for(std::int64_t i = on + 250; i < off; ++i)
            CHECK(reference.out[static_cast<std::size_t>(i)] == 0.0f);
        CHECK(reference.rendered < unswitched.rendered);
        // The note struck while resting was dropped, the one struck while fading played;
        // the release while resting acted (72 let go, 67 sounds).
        CHECK(RunSwitches(make(false, true, true), total, [] { return 512; }).out == reference.out);
        CHECK(RunSwitches(make(true, false, true), total, [] { return 512; }).out != reference.out);
        CHECK(std::fabs(reference.pitch - std::pow(2.0f, 7.0f / 12.0f)) < 1e-5f);
        // The blip within one block is heard (it does not vanish into the block's end).
        CHECK(RunSwitches(make(true, true, false), total, [] { return 2048; }).out != reference.out);
    }
    std::printf("switches on the sample: Stop and Bypass act at their own sample, the same output for "
                "blocks of 2048, 1024, 512, 1, varying and 333/1000; a blip inside one block is heard; "
                "while resting a strike is dropped, a release and a point act when it goes on\n");
}

// MS-R34, MS-R36 (0.1.10): the screen keyboard and the host's MIDI on the keyboard's
// channel share the engine's note; it ends only when the last of them lets go, also at
// a disconnect. While the engine rests, a screen key is dropped like a host note, and
// no MIDI goes out for it.
bool NoteOnOut(PluginHost& host, int pitch) {
    for(int32 i = 0; i < host.OutEvents().getEventCount(); ++i) {
        Event e{};
        host.OutEvents().getEvent(i, e);
        if(e.type == Event::kNoteOnEvent && e.noteOn.pitch == pitch)
            return true;
    }
    return false;
}

void TestScreenAndHostKeys() {
    const auto ch = static_cast<int>(Processor::kKeyboardChannel);
    const HostScore score = {{4800, Ev::kNoteOn, 72, 0.8, ch},   {5824, Ev::kNoteOff, 72, 0.0, ch},
                             {6336, Ev::kNoteOn, 72, 0.8, ch},   {7360, Ev::kNoteOff, 72, 0.0, ch}};
    IPtr<HostApplication> app = owned(new HostApplication());
    PluginHost host({}, score);
    IPtr<Controller> controller = owned(new Controller());
    CHECK(controller->initialize(app) == kResultOk);
    auto* processor_point = static_cast<IConnectionPoint*>(host.processor.get());
    auto* controller_point = static_cast<IConnectionPoint*>(controller.get());
    CHECK(processor_point->connect(controller_point) == kResultOk);
    CHECK(controller_point->connect(processor_point) == kResultOk);
    CHECK(LoadState(*host.processor, MakeState(StartValues(), g_path)) == kResultOk);
    host.Run(4800, 256);
    auto pitch = [&] { return host.processor->Engine().EffectivePitch(); };
    // The host holds 72; the screen strikes and lets go of it: the host's note stays.
    host.Run(256, 256);
    CHECK(pitch() == 2.0f);
    CHECK(controller->PlayNote(72));
    host.Run(256, 256);
    CHECK(controller->ReleaseNote(72));
    host.Run(256, 256);
    CHECK(pitch() == 2.0f);
    // The screen holds 72 and the host lets go of it: the screen's note stays.
    CHECK(controller->PlayNote(72));
    host.Run(256, 256);
    host.Run(256, 256); // 5824: the host's release
    CHECK(pitch() == 2.0f);
    CHECK(controller->ReleaseNote(72));
    host.Run(256, 256);
    CHECK(pitch() == 1.0f);
    // Codex' case: both hold 72, then a disconnect releases the screen's key; the host's
    // note sounds on.
    host.Run(256, 256); // 6336: the host strikes
    CHECK(controller->PlayNote(72));
    host.Run(256, 256);
    CHECK(processor_point->disconnect(controller_point) == kResultOk);
    CHECK(controller_point->disconnect(processor_point) == kResultOk);
    host.Run(512, 256);
    CHECK(pitch() == 2.0f);
    host.Run(512, 256); // 7360: the host lets go
    CHECK(pitch() == 1.0f);

    // MS-R36: Stop rests the engine. A screen key struck now is dropped, without MIDI
    // out; one held from before is let go by its release.
    CHECK(processor_point->connect(controller_point) == kResultOk);
    CHECK(controller_point->connect(processor_point) == kResultOk);
    CHECK(controller->PlayNote(64));
    host.Run(256, 256);
    CHECK(std::fabs(pitch() - std::pow(2.0f, 4.0f / 12.0f)) < 1e-6f);
    ParameterChanges changes(4);
    host.external_changes = &changes;
    int32 index = 0;
    changes.addParameterData(kParamStop, index)->addPoint(0, 1.0, index);
    host.Process(256);
    changes.clearQueue();
    host.Run(768, 256); // faded out: resting
    CHECK(controller->PlayNote(72));
    host.Run(256, 256);
    CHECK(!NoteOnOut(host, 72));
    CHECK(controller->ReleaseNote(64));
    CHECK(controller->ReleaseNote(72));
    host.Run(256, 256);
    changes.addParameterData(kParamStop, index)->addPoint(0, 0.0, index);
    host.Process(256);
    changes.clearQueue();
    host.Run(512, 256);
    CHECK(pitch() == 1.0f); // neither 72 (dropped) nor 64 (let go while resting)
    host.external_changes = nullptr;
    CHECK(processor_point->disconnect(controller_point) == kResultOk);
    CHECK(controller_point->disconnect(processor_point) == kResultOk);
    CHECK(controller->terminate() == kResultOk);
    CHECK(host.allocations == 0);
    std::printf("screen and host keys: the shared note ends when the last holder lets go, also at a "
                "disconnect; while resting a screen key is dropped without MIDI out, a release acts\n");
}

// A long pause with a busy MIDI track (0.1.10): while the engine rests, the adapter's
// note queue does not drain. Strikes are dropped there; releases of notes nobody holds
// are not passed on, so they cannot fill the queue and push out the release of a note
// that does sound.
void TestLongPause() {
    const std::int64_t stop = 5000, junk = 6000, resume = 14000;
    HostScore score = {{4800, Ev::kNoteOn, 72, 0.8, 2},
                       {stop, Ev::kParam, static_cast<int>(kParamStop), 1.0}};
    std::int64_t at = junk;
    auto busy = [&](int count) { // a track playing on while stopped
        for(int i = 0; i < count; ++i, at += 3) {
            score.push_back({at, Ev::kNoteOn, 40 + i % 40, 0.7, 3});
            score.push_back({at + 1, Ev::kNoteOff, 40 + i % 40, 0.0, 3});
        }
    };
    busy(1100); // more releases than the queue holds
    score.push_back({at, Ev::kNoteOff, 72, 0.0, 2}); // the held note's release
    at += 3;
    busy(50);
    score.push_back({resume, Ev::kParam, static_cast<int>(kParamStop), 0.0});
    PluginHost host({}, score);
    CHECK(LoadState(*host.processor, MakeState(StartValues(), g_path)) == kResultOk);
    host.Run(4800, 256);
    host.Run(256, 256);
    CHECK(host.processor->Engine().EffectivePitch() == 2.0f);
    host.Run(resume + 2048 - host.position, 256);
    CHECK(host.processor->Engine().EffectivePitch() == 1.0f); // 72 was let go
    CHECK(host.processor->Adapter().DroppedNotes() == 0);
    CHECK(host.allocations == 0);

    // MS-R38 (0.1.11), Codex' case taken to its end: every note that can be held is held
    // (16 channels of 128), all but one are let go while the engine rests. The queue holds
    // all those releases: no "all notes off" ends the note still held.
    HostScore all;
    std::int64_t t = 4800;
    for(int channel = 0; channel < 16; ++channel)
        for(int note = 0; note < 128; ++note)
            if(!(channel == 15 && note == 72))
                all.push_back({t++, Ev::kNoteOn, note, 0.5, channel});
    all.push_back({t++, Ev::kNoteOn, 72, 0.8, 15}); // the newest: it sounds, pitch 2
    const std::int64_t stopped = t + 512;
    all.push_back({stopped, Ev::kParam, static_cast<int>(kParamStop), 1.0});
    t = stopped + 1000;
    for(int channel = 0; channel < 16; ++channel)
        for(int note = 0; note < 128; ++note)
            if(!(channel == 15 && note == 72))
                all.push_back({t++, Ev::kNoteOff, note, 0.0, channel});
    const std::int64_t resumed = t + 1000;
    all.push_back({resumed, Ev::kParam, static_cast<int>(kParamStop), 0.0});
    PluginHost crowd({}, all);
    CHECK(LoadState(*crowd.processor, MakeState(StartValues(), g_path)) == kResultOk);
    crowd.Run(stopped, 256);
    CHECK(crowd.processor->Engine().EffectivePitch() == 2.0f);
    crowd.Run(resumed + 2048 - crowd.position, 256);
    CHECK(crowd.processor->Adapter().DroppedNotes() == 0);
    CHECK(crowd.processor->Engine().EffectivePitch() == 2.0f); // 72 on channel 16 still held
    CHECK(crowd.allocations == 0);
    std::printf("long pause: a busy track while stopped neither fills the note queue nor loses the "
                "release of the note held; all 2047 other held notes let go while resting, the "
                "last one sounds on\n");
}

// The resize grip (0.1.11): a press starts a drag, moves report the distance from the
// press in window pixels, the release ends it. The window's own size rules
// (ConstrainEditorSize, as for a corner drag) are tested in TestViewLayout.
void TestResizeGrip() {
    int begun = 0;
    std::vector<std::pair<double, double>> moved;
    auto grip = VSTGUI::makeOwned<ResizeGrip>(VSTGUI::CRect(0, 0, 18, 18), VSTGUI::CColor(150, 156, 168),
                                              [&] { ++begun; },
                                              [&](double dx, double dy) { moved.push_back({dx, dy}); });
    grip->DragTo(10, 10); // no press: nothing
    CHECK(moved.empty() && !grip->Dragging());
    grip->Press(880, 520);
    CHECK(begun == 1 && grip->Dragging());
    grip->DragTo(920, 545);
    grip->DragTo(860, 500);
    CHECK(moved.size() == 2 && moved[0] == std::make_pair(40.0, 25.0) && moved[1] == std::make_pair(-20.0, -20.0));
    grip->Let();
    grip->DragTo(1000, 1000);
    CHECK(moved.size() == 2 && !grip->Dragging());
    // A corner drag of 40 by 25 pixels from 100 %: a size of the window's aspect ratio.
    const EditorSize start = EditorSizeFor(1.0, true, false);
    const EditorSize asked{start.width + 40, start.height + 25};
    const EditorSize allowed = ConstrainEditorSize(asked, start, 1.0, true, false, true);
    CHECK(allowed.width > start.width && allowed.height > start.height);
    CHECK(std::fabs(static_cast<double>(allowed.width) / allowed.height
                    - static_cast<double>(start.width) / start.height) < 0.01);
    std::printf("resize grip: a drag reports its distance from the press; the window follows it as "
                "a corner drag, with its aspect ratio\n");
}

// MS-R39 (0.1.11): starting the LOAD... dialog. If the system cannot open it, LOAD...
// works again at once and the reference VSTGUI took for the answer is given back; a
// dialog that answered (also by cancelling) or is still open is left alone.
void TestDialogStart() {
    bool open = false;
    int given_back = 0;
    auto back = [&] { ++given_back; };
    auto alive = std::make_shared<bool>(true);
    // The system could not create the dialog: no answer.
    auto answered = std::make_shared<bool>(false);
    CHECK(!StartDialog(open, alive, answered, [] { return false; }, back));
    CHECK(!open && given_back == 1);
    // A second attempt works: the dialog opens and is cancelled (Windows answers, then
    // returns false).
    answered = std::make_shared<bool>(false);
    CHECK(StartDialog(open, alive, answered, [&] {
        CHECK(open); // open while it runs
        *answered = true;
        open = false; // the answer's own reset
        return false;
    }, back));
    CHECK(!open && given_back == 1);
    // Started and still open (a platform that answers later): stays open.
    answered = std::make_shared<bool>(false);
    CHECK(StartDialog(open, alive, answered, [] { return true; }, back));
    CHECK(open && given_back == 1);
    open = false;
    // run() throws before an answer: reset, given back, the exception goes on to the
    // button's guard.
    answered = std::make_shared<bool>(false);
    bool thrown = false;
    try {
        StartDialog(open, alive, answered, []() -> bool { throw std::bad_alloc(); }, back);
    } catch(const std::bad_alloc&) {
        thrown = true;
    }
    CHECK(thrown && !open && given_back == 2);
    // GA.2 (0.2.10): the host destroyed the editor during the dialog's loop, then run()
    // fails without an answer. `open` is the dead editor's: not written (it stays as the
    // start left it), and the reference still goes back.
    answered = std::make_shared<bool>(false);
    thrown = false;
    try {
        StartDialog(open, alive, answered, [&]() -> bool { *alive = false; throw std::bad_alloc(); }, back);
    } catch(const std::bad_alloc&) {
        thrown = true;
    }
    CHECK(thrown && open && given_back == 3);
    *alive = true;
    open = false;
    answered = std::make_shared<bool>(false);
    CHECK(!StartDialog(open, alive, answered, [&] { *alive = false; return false; }, back));
    CHECK(open && given_back == 4);
    std::printf("dialog start: a dialog that cannot open leaves LOAD... usable and gives its reference "
                "back; an answered or open dialog is left alone\n");
}

// The map takes the mouse (0.1.8): a click or drag sets START to that place in the
// file, the wheel steps SPRAY; without a file a click does nothing. Beta 2: on the round
// glass, through the lens; outside it, the nearest point of its rim.
void TestMapEdits() {
    // A glass of radius 180 centred at (218, 218): on its middle line a quarter of the file
    // lies 0.6 of the radius left of the centre (the lens enlarges the middle by 1.5).
    auto map = VSTGUI::makeOwned<MapView>(VSTGUI::CRect(0, 0, 436, 436));
    CHECK(map->GlassRadius() == 180.0 && map->GlassCentre() == VSTGUI::CPoint(218, 218));
    std::vector<std::string> calls;
    MapView::Edits edits;
    edits.start_begin = [&] { calls.push_back("begin"); };
    edits.start = [&](double share) { char t[32]; std::snprintf(t, sizeof t, "start %.3f", share); calls.push_back(t); };
    edits.start_end = [&] { calls.push_back("end"); };
    edits.spray = [&](double step) { char t[32]; std::snprintf(t, sizeof t, "spray %+.3f", step); calls.push_back(t); };
    map->SetEdits(std::move(edits));
    CHECK(!map->PressAt(218) && calls.empty()); // no file yet
    EngineDisplay display;
    display.sequence = 1;
    display.frames = 48000;
    display.loop_length = 48000;
    map->Show(display, nullptr);
    CHECK(map->PressAt(218));
    map->DragTo(-30);
    map->DragTo(2000);
    map->DragTo(218 - 0.6 * 180);
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
    CHECK(white == kKeyboardWhiteKeys && black == kKeyboardBlackKeys);
    // Round 3: one octave of pads, moved over C2..C8 as the two octaves were.
    CHECK(ClampKeyboardTranspose(-36) == -24 && ClampKeyboardTranspose(13) == 12 && ClampKeyboardTranspose(60) == 36);
    CHECK(kKeyboardLowNote + kKeyboardLowestTranspose == 36 && kKeyboardHighNote + kKeyboardHighestTranspose == 108);
    {
        int pads = 0;
        for(int note = kKeyboardLowNote; note <= kKeyboardHighNote; ++note) {
            const KeyRect pad = PadRectFor(note, width, height);
            CHECK(pad.left >= 0.0 && pad.right <= width + 1e-9 && (pad.top == 0.0) == IsBlackKey(note));
            pads += PadNoteAt((pad.left + pad.right) / 2, (pad.top + pad.bottom) / 2, width, height) == note ? 1 : 0;
        }
        CHECK(pads == kKeyboardWhiteKeys + kKeyboardBlackKeys);
        // C# and D# apart, with a gap between them; above E-F, between the black pads: nothing.
        CHECK(PadRectFor(kKeyboardLowNote + 1, width, height).right < PadRectFor(kKeyboardLowNote + 3, width, height).left);
        const double white_width = width / kKeyboardWhiteKeys;
        CHECK(PadNoteAt(3 * white_width, height * 0.25, width, height) == -1);
    }
    const auto same = [](EditorSize a, EditorSize b) { return a.width == b.width && a.height == b.height; };
    // Beta 2: 624 high (0.2.4: 560), 84 more with the keyboard, 96 more with DETAILS open.
    CHECK(same(EditorSizeFor(1.0, false, false), {880, 624}) && same(EditorSizeFor(1.0, true, false), {880, 708})
          && same(EditorSizeFor(1.0, true, true), {880, 804}));
    using Clock = DragConstraint::Clock;
    Clock::time_point now{};
    DragConstraint drag;
    now += std::chrono::milliseconds(10);
    CHECK(same(drag.Apply({1320, 624}, {880, 624}, 1.0, false, false, now), {1320, 936}));
    for(const bool keyboard : {false, true})
        for(const bool details : {false, true})
            for(int w = 200; w <= 1600; w += 7)
                for(int h = 80; h <= 900; h += 31) {
                    const EditorSize size =
                        ConstrainEditorSize({w, h}, EditorSizeFor(1.0, keyboard, details), 1.0, keyboard, details, true);
                    CHECK(std::abs(size.height - size.width * EditorBaseHeight(keyboard, details) / kEditorWidth) < 1.0);
                    CHECK(size.width >= 440 && size.width <= 1760);
                }
    for(const bool keyboard : {false, true})
        for(const double scale : {1.0, 1.5, 2.0})
            for(const double zoom : {0.5, 1.0, 1.3, 2.0}) {
                const EditorSize size = EditorSizeFor(scale * zoom, keyboard, true);
                CHECK(std::fabs(EditorZoomForSize(size.width, size.height, scale, keyboard, true) - zoom) < 0.005);
            }
    std::printf("layout: 25 keys where they are drawn; window 880 x 624 (+84 keys, +96 DETAILS), aspect "
                "ratio and zoom limits kept\n");
}


// --- Review findings MS-R02 to MS-R04 (Codex, 2026-11-17) --------------------

// A host's component handler that puts every edit at sample 0 of the next block,
// keeping one value per parameter and position as the SDK's queue does
// (ParameterValueQueue::addPoint replaces a point at the same offset). Counts
// setDirty() calls.
// Counts the edits that reach the host (0.2.9, C3).
class CountingHandler : public U::Implements<U::Directly<IComponentHandler>> {
  public:
    tresult PLUGIN_API beginEdit(ParamID) override {
        ++begins;
        return kResultOk;
    }
    tresult PLUGIN_API performEdit(ParamID, ParamValue) override { return kResultOk; }
    tresult PLUGIN_API endEdit(ParamID) override {
        ++ends;
        return kResultOk;
    }
    tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }
    int begins = 0, ends = 0;
};

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
// Beta 2 (the user's round window): the lens lays the plate onto the glass and back. Its
// edges are the rim (the file's start the left arc, its end the right one, L the top), the
// middle line stays a line across, enlarged 1.5 times at the centre; a point of the glass
// gives back its place in the file, a point outside it the nearest place on the rim.
void TestLens() {
    using namespace messier::lens;
    const auto close_to = [](double a, double b) { return std::abs(a - b) < 1e-9; };
    CHECK(close_to(ToGlass(0.5, 0).x, 0) && close_to(ToGlass(0.5, 0).y, 0));
    for(const double share : {0.0, 1.0})
        for(const double down : {-1.0, -0.4, 0.0, 0.7, 1.0})
            CHECK(close_to(std::hypot(ToGlass(share, down).x, ToGlass(share, down).y), 1.0));
    CHECK(close_to(std::hypot(ToGlass(0.3, -1.0).x, ToGlass(0.3, -1.0).y), 1.0) && ToGlass(0.3, -1.0).y < 0);
    CHECK(close_to(ToGlass(0.0, 0).x, -1) && close_to(ToGlass(1.0, 0).x, 1) && close_to(ToGlass(0.25, 0).x, -0.6));
    CHECK(std::abs(ToGlass(0.5 + 1e-6, 0).x / 2e-6 - 1.5) < 1e-4); // the centre's enlargement
    for(const double share : {0.0, 0.1, 0.37, 0.5, 0.81, 1.0})
        for(const double down : {-0.9, -0.3, 0.0, 0.45, 0.9}) {
            const Point glass = ToGlass(share, down);
            const Point back = FromGlass(glass.x, glass.y);
            CHECK(std::abs(back.x - share) < 1e-6 && std::abs(back.y - down) < 1e-6);
        }
    CHECK(close_to(FromGlass(-3.0, 0).x, 0) && close_to(FromGlass(2.0, 0).x, 1) && close_to(FromGlass(0, -5).y, -1));
    // Monotonic across the middle line, so a drag goes one way through the file.
    double last = -2;
    for(int k = 0; k <= 100; ++k) {
        const double x = ToGlass(k / 100.0, 0).x;
        CHECK(x > last);
        last = x;
    }
    CHECK(PanDown(0) == -kPanReach && PanDown(1) == kPanReach && PanDown(0.5) == 0);
    std::printf("lens: the plate fills the glass, edges on the rim, middle 1.5x, every place found back\n");
}

// Beta 2 (the user's wishes): GALAXY's stars fly out from near the centre while the
// playback goes forward, in from the rim backward, and stand at 0x; ever faster further
// out; each in a direction of its own, the same every time, all round the glass.
void TestStarfield() {
    int quarters[4] = {};
    for(std::uint32_t n = 1; n <= 400; ++n) {
        const MapView::Star a = MapView::StarDirection(n), b = MapView::StarDirection(n);
        CHECK(a.x == b.x && a.y == b.y && std::abs(std::hypot(a.x, a.y) - 1.0) < 1e-9);
        ++quarters[(a.x >= 0 ? 0 : 1) + (a.y >= 0 ? 0 : 2)];
    }
    for(const int q : quarters)
        CHECK(q > 70); // not only to the left and right
    CHECK(MapView::StarStart(7, 1.0) < 0.05 && MapView::StarStart(7, -1.0) > 0.95);
    const double still = MapView::StarStart(7, 0.0);
    CHECK(still >= 0.15 && still <= 0.9 && MapView::StarFly(still, 0.0, 1.0) == still);
    CHECK(MapView::StarFly(0.5, 1.0, 0.1) > 0.5 && MapView::StarFly(0.5, -1.0, 0.1) < 0.5);
    CHECK(MapView::StarFly(0.8, 1.0, 0.1) - 0.8 > MapView::StarFly(0.2, 1.0, 0.1) - 0.2); // faster further out
    CHECK(MapView::StarFly(MapView::StarStart(7, 1.0), 1.0, 1.45) >= 0.99);  // about 1.4 s at +1x
    CHECK(MapView::StarFly(MapView::StarStart(7, 2.0), 2.0, 0.75) >= 0.99);  // twice as fast at +2x
    // Round 4: a short grain's star keeps its shape (rays) for kStarMinLife, then glows on;
    // its light runs on without a jump, its rays fade over the glow and end with it.
    {
        using S = MapView;
        const double short_life = 0.03, end = S::kStarMinLife + S::kStarGlow;
        CHECK(S::StarLook(0.3, short_life).rays == 1.0 && S::StarLook(0.3, short_life).bright >= 0.8);
        const S::StarLight before = S::StarLook(S::kStarMinLife - 1e-6, short_life);
        const S::StarLight after = S::StarLook(S::kStarMinLife + 1e-6, short_life);
        CHECK(std::abs(before.bright - after.bright) < 1e-3 && std::abs(before.rays - after.rays) < 1e-3);
        CHECK(S::StarLook(S::kStarMinLife + 0.5 * S::kStarGlow, short_life).rays > 0.1
              && S::StarLook(S::kStarMinLife + 0.5 * S::kStarGlow, short_life).rays < 0.5);
        CHECK(S::StarLook(end, short_life).bright == 0.0 && S::StarLook(end, short_life).rays == 0.0);
        CHECK(S::StarLook(end - 0.01, short_life).rays < 1e-3);
        // A long grain keeps its shape while it sounds.
        CHECK(S::StarLook(0.9, 1.0).rays == 1.0 && S::StarLook(1.0 + S::kStarGlow, 1.0).bright == 0.0);
    }
    // Round 5: the turn's speed starts and stops along an S: hardly moving at first (slower
    // than the first-order ease of before), never past its target, close to it after 5 s,
    // and from full speed to rest the same way.
    {
        double speed = 0, rate = 0, last = 0;
        bool monotonic = true;
        for(int k = 1; k <= 400; ++k) {
            MapView::EaseTurn(speed, rate, 1.0, 0.025);
            if(k == 4)
                CHECK(speed < 0.25 * (1.0 - std::exp(-0.1 / MapView::kTurnEase)));
            monotonic = monotonic && speed >= last && speed <= 1.0;
            last = speed;
            if(k == 200)
                CHECK(speed > 0.95);
        }
        CHECK(monotonic);
        for(int k = 0; k < 800; ++k)
            MapView::EaseTurn(speed, rate, 0.0, 0.025);
        CHECK(speed == 0.0 && rate == 0.0);
    }
    // Round 5: GALAXY / WAVE round BLEND: the left arc and name choose GALAXY, the right ones
    // WAVE, the knob in the middle neither.
    {
        auto arcs = VSTGUI::makeOwned<ViewSwitch>(VSTGUI::CRect(0, 0, 237, 50), [](bool) {});
        const double cx = 237 / 2.0, cy = 25;
        CHECK(arcs->SideAt(cx - layout::kArcRadius, cy) == -1 && arcs->SideAt(cx + layout::kArcRadius, cy) == 1);
        CHECK(arcs->SideAt(10, cy) == -1 && arcs->SideAt(227, cy) == 1); // the names
        CHECK(arcs->SideAt(cx, cy) == 0 && arcs->SideAt(cx + 20, cy + 5) == 0); // the knob
        CHECK(arcs->PressAt(cx + layout::kArcRadius, cy) && !arcs->Galaxy() && !arcs->PressAt(cx, cy));
    }
    // MS-R63: the fast turn. A disc turned by 0 degrees stays itself; by 90 degrees each
    // pixel lands where an exact quarter turn puts it; the corners outside the disc stay clear,
    // also when the buffer is reused.
    {
        PixelBuffer from;
        from.width = from.height = 40;
        from.bytes.assign(40 * 40 * 4, 0);
        for(std::uint32_t y = 0; y < 40; ++y)
            for(std::uint32_t x = 0; x < 40; ++x)
                if(std::hypot(x + 0.5 - 20, y + 0.5 - 20) < 18)
                    for(int k = 0; k < 4; ++k)
                        from.bytes[(y * 40 + x) * 4 + k] = static_cast<std::uint8_t>((x * 7 + y * 13 + k * 31) % 256);
        PixelBuffer to;
        TurnPixels(from, 0.0, to);
        CHECK(to.bytes == from.bytes);
        TurnPixels(from, 90.0, to); // reused
        int wrong = 0;
        for(std::uint32_t y = 0; y < 40; ++y)
            for(std::uint32_t x = 0; x < 40; ++x)
                for(int k = 0; k < 4; ++k) // clockwise: the source of (x, y) is (y, 39 - x)
                    wrong += std::abs(to.bytes[(y * 40 + x) * 4 + k] - from.bytes[((39 - x) * 40 + y) * 4 + k]) > 1;
        CHECK(wrong == 0);
        CHECK(to.bytes[0] == 0 && to.bytes[3] == 0 && to.bytes[(40 * 40 - 1) * 4 + 3] == 0);
        // Its time for the painted nebula at 2x (720 x 720; MS-R63 measured 24 ms before).
        PixelBuffer big, turned;
        big.width = big.height = 720;
        big.bytes.assign(720 * 720 * 4, 200);
        TurnPixels(big, 1.0, turned);
        const auto t0 = std::chrono::steady_clock::now();
        for(int k = 0; k < 10; ++k)
            TurnPixels(big, 10.0 + k, turned);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 10;
        std::printf("turn of 720 x 720 pixels: %.1f ms\n", ms);
    }
    // MS-R66: the glass is drawn again only while something on it moves.
    {
        auto view = VSTGUI::makeOwned<MapView>(VSTGUI::CRect(0, 0, 436, 436));
        const auto now = MapView::Clock::now();
        CHECK(!view->Moving(now)); // no file
        EngineDisplay d;
        d.sequence = 1;
        d.frames = 48000;
        d.loop_length = 48000;
        view->ShowAt(d, nullptr, now);
        CHECK(!view->Moving(now)); // a file at rest: no grains, no playhead speed
        d.grain_count = 1;
        d.grains[0] = {100.0f, 0.5f, 0.2f, 48000.0f, 10.0f, 7};
        d.rendered = 2;
        view->ShowAt(d, nullptr, now);
        CHECK(view->Moving(now));
        const VSTGUI::CRect box = view->GlassBox();
        CHECK(box.getWidth() < 436 && box.getWidth() >= 2 * (view->GlassRadius() + layout::kRingWidth));
    }
    // The WAVE redesign, step 0: the columns all layers draw. Each covers its share of the
    // overview, takes its widest peaks and loudest RMS, and the body never exceeds the peaks.
    {
        auto wave = std::make_unique<WaveOverview>();
        wave->columns = 8;
        for(int k = 0; k < 8; ++k) {
            wave->high[k] = static_cast<std::int16_t>(1000 * k);
            wave->low[k] = static_cast<std::int16_t>(-500 * k);
            wave->rms[k] = static_cast<std::uint16_t>(300 * k);
        }
        wave->rms[1] = 30000; // louder than its peaks: held at the peak
        std::vector<MapView::WaveColumn> columns;
        MapView::WaveColumns(*wave, 4, columns);
        CHECK(columns.size() == 4 && std::abs(columns[0].share - 0.125) < 1e-12 && std::abs(columns[3].share - 0.875) < 1e-12);
        const double unit = MapView::kWaveHeight / 32768.0;
        CHECK(std::abs(columns[2].top + 5000 * unit) < 1e-12 && std::abs(columns[2].bottom - 2500 * unit) < 1e-12);
        CHECK(std::abs(columns[2].body - 1500 * unit) < 1e-12);
        CHECK(std::abs(columns[0].body - 1000 * unit) < 1e-12); // RMS 30000 held at the peak 1000
        MapView::WaveColumns(*wave, 16, columns); // more columns than the overview: each its own
        CHECK(columns.size() == 16 && std::abs(columns[15].top + 7000 * unit) < 1e-12);
        wave->columns = 0;
        MapView::WaveColumns(*wave, 16, columns);
        CHECK(columns.empty());
    }
    // Step 2: the depth layer's blur. A lone pixel spreads evenly either way and keeps its
    // light; an even area stays even inside; a radius of 0 changes nothing.
    {
        PixelBuffer dot;
        dot.width = dot.height = 41;
        dot.bytes.assign(41 * 41 * 4, 0);
        for(int k = 0; k < 4; ++k)
            dot.bytes[(20 * 41 + 20) * 4 + k] = 255;
        PixelBuffer same = dot;
        BlurPixels(same, 0);
        CHECK(same.bytes == dot.bytes);
        // Spread more widely first, so that rounding keeps the sum: a 5 x 5 block.
        for(int y = 18; y <= 22; ++y)
            for(int x = 18; x <= 22; ++x)
                for(int k = 0; k < 4; ++k)
                    dot.bytes[(y * 41 + x) * 4 + k] = 255;
        long before = 0, after = 0;
        for(std::size_t i = 3; i < dot.bytes.size(); i += 4)
            before += dot.bytes[i];
        BlurPixels(dot, 2);
        for(std::size_t i = 3; i < dot.bytes.size(); i += 4)
            after += dot.bytes[i];
        const auto alpha = [&](int x, int y) { return dot.bytes[(y * 41 + x) * 4 + 3]; };
        CHECK(std::abs(after - before) < before / 50);
        CHECK(alpha(20, 20) < 255 && alpha(20, 20) > alpha(26, 20) && alpha(26, 20) > 0);
        CHECK(alpha(14, 20) == alpha(26, 20) && alpha(20, 14) == alpha(20, 26) && std::abs(alpha(26, 20) - alpha(20, 26)) <= 1);
        CHECK(alpha(0, 0) == 0); // beyond three boxes of 2: untouched
        PixelBuffer even;
        even.width = even.height = 30;
        even.bytes.assign(30 * 30 * 4, 120);
        BlurPixels(even, 3);
        CHECK(even.bytes[(15 * 30 + 15) * 4] == 120 && even.bytes[3] < 120); // the edges fade
    }
    std::printf("starfield: out forward, in backward, standing at 0x, faster further out; directions all round\n");
}

// Beta 2: HUE from red over violet to blue; a hue of beta 1 outside that arc is shown at
// its nearer end; HUE turns the nebula and the accents, not the labels and values.
void TestHueScale() {
    CHECK(AccentOfSlider(0.0) == 0 && AccentOfSlider(1.0) == kAccentBlueHue && AccentOfSlider(0.5) == 295);
    CHECK(AccentSliderValue(0) == 0.0 && AccentSliderValue(kAccentBlueHue) == 1.0 && AccentSliderValue(295) == 0.5);
    for(const double value : {0.0, 0.1, 0.33, 0.5, 0.9, 1.0})
        CHECK(std::abs(AccentSliderValue(AccentOfSlider(value)) - value) < 0.5 / (360 - kAccentBlueHue));
    CHECK(AccentOnScale(30) == 0 && AccentOnScale(120) == kAccentBlueHue && AccentOnScale(285) == 285);
    CHECK(IsAccent(359) && !IsAccent(360) && IsAccent(kPresetAccent));
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // as TestCustomSkin
    VSTGUI::init(GetModuleHandleW(nullptr));
    if(auto* win32 = VSTGUI::getPlatformFactory().asWin32Factory())
        win32->setResourceBasePath(VSTGUI::UTF8String(MESSIER_GUI_DIR "/"));
    {
    Skin skin(GuiPreset::kSpiral, {});
    const SkinColours before = skin.Colours();
    skin.SetAccent(0); // red
    const SkinColours& after = skin.Colours();
    CHECK(after.text == before.text && after.button_text == before.button_text && after.dim == before.dim
          && after.control != before.control); // the accents follow HUE, the names stay neutral
    // Round 5: the values follow HUE as well, at their own lightness.
    double h = 0, sat = 0, light = 0, light_before = 0;
    before.value.toHSL(h, sat, light_before);
    after.value.toHSL(h, sat, light);
    CHECK(after.value != before.value && std::lround(h) % 360 == 0 && std::abs(light - light_before) < 0.01);
    after.nebula_a.toHSL(h, sat, light);
    CHECK(std::lround(h) % 360 == 0 && after.nebula_a != before.nebula_a && after.start_fill != before.start_fill);
    }
    // MS-R69 (needs VSTGUI, as the skin above): a knob takes the mouse on its round cap only, not in its box's corners.
    {
        auto knob = VSTGUI::makeOwned<MetalKnob>(VSTGUI::CRect(10, 10, 82, 82), nullptr, 0, false, false);
        VSTGUI::MouseDownEvent event;
        CHECK(knob->hitTest(VSTGUI::CPoint(46, 46), event) && knob->hitTest(VSTGUI::CPoint(46, 12), event));
        CHECK(!knob->hitTest(VSTGUI::CPoint(13, 13), event) && !knob->hitTest(VSTGUI::CPoint(80, 80), event));
    }

    VSTGUI::exit();
    if(SUCCEEDED(com))
        CoUninitialize();
    std::printf("HUE: red to violet to blue, old hues at the nearer end; only the nebula and START's fill\n");
}

// Beta 2: the round window's layout. Every primary knob, its name, value and scale stay
// clear of the glass' ring and of each other, inside the window and between the header and
// the START row; the START row, FREEZE, RESET and the info line inside the margins.
void TestRoundLayout() {
    namespace L = layout;
    const auto overlap = [](const L::Box& a, const L::Box& b) {
        return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
    };
    // The farthest a box's corner may come to the glass' centre: outside the ring, with air.
    const auto clear_of_ring = [](const L::Box& b) {
        const double x = std::clamp(L::kGlassX, b.left, b.right), y = std::clamp(L::kGlassY, b.top, b.bottom);
        return std::hypot(x - L::kGlassX, y - L::kGlassY) >= L::kRingRadius + 4;
    };
    std::vector<L::Box> boxes;
    for(int slot = 0; slot < L::kPrimaryCount; ++slot) {
        for(const L::Box& b : {L::KnobBox(slot), L::NameBox(slot), L::ValueBox(slot)}) {
            CHECK(b.left >= L::kMargin / 2 && b.right <= L::kWidth - L::kMargin / 2);
            CHECK(b.top > L::kHeaderRule && b.bottom < L::kStartTop);
            CHECK(slot == L::kBelowGlass ? clear_of_ring(L::NameBox(slot)) : clear_of_ring(b));
            boxes.push_back(b);
        }
        if(slot != L::kBelowGlass)
            for(const bool high : {false, true})
                CHECK(clear_of_ring(L::ScaleBox(slot, high)) && L::ScaleBox(slot, high).left >= L::kMargin / 2
                      && L::ScaleBox(slot, high).right <= L::kWidth - L::kMargin / 2);
    }
    // BLEND below the glass: its knob may touch the ring, never the glass.
    {
        const L::Box b = L::KnobBox(L::kBelowGlass);
        CHECK(b.top >= L::kGlassY + L::kGlassRadius);
    }
    for(std::size_t i = 0; i < boxes.size(); ++i)
        for(std::size_t j = i + 1; j < boxes.size(); ++j)
            CHECK(i / 3 == j / 3 || !overlap(boxes[i], boxes[j]));
    // The plate view: the ring and the axis inside it, below the header.
    CHECK(L::kMapTop >= L::kHeaderRule && L::kMapRight - L::kMapLeft == 2 * L::kMapHalf);
    // Round 5: DETAILS top left, FREEZE and RESET above the START row, the START track
    // centred between its two names, and GALAXY / WAVE round BLEND: all clear of the knobs,
    // their names, values and scales (the arcs lie behind BLEND's own knob only), of each
    // other and of the glass' ring.
    std::vector<L::Box> scales;
    for(int slot = 0; slot < L::kPrimaryCount; ++slot)
        if(slot != L::kBelowGlass)
            for(const bool high : {false, true})
                scales.push_back(L::ScaleBox(slot, high));
    for(const bool high : {false, true})
        scales.push_back(L::ScaleBox(L::kBelowGlass, high)); // BLEND's, drawn below its knob
    const L::BottomRows r = L::Bottom();
    const L::Box arcs = L::ArcSwitchBox();
    const std::array<L::Box, 4> keys{r.details, r.freeze, r.reset, arcs};
    for(std::size_t k = 0; k < keys.size(); ++k) {
        CHECK(keys[k].left >= L::kMargin && keys[k].right <= L::kWidth - L::kMargin && keys[k].top >= L::kHeaderRule
              && keys[k].bottom <= L::kStartTop);
        for(std::size_t i = 0; i < boxes.size(); ++i)
            CHECK(!overlap(keys[k], boxes[i]) || (k == 3 && i == 3 * L::kBelowGlass)); // the arcs: behind BLEND
        for(const L::Box& scale : scales)
            CHECK(!overlap(keys[k], scale));
        for(std::size_t j = k + 1; j < keys.size(); ++j)
            CHECK(!overlap(keys[k], keys[j]));
    }
    for(const L::Box& key : {r.details, r.freeze, r.reset})
        CHECK(key.right - key.left == L::kButtonWidth && key.bottom - key.top == L::kButtonHeight);
    for(const double angle : {L::kGalaxyArcFrom, L::kGalaxyArcTo, L::kWaveArcFrom, L::kWaveArcTo}) {
        const double a = angle * 3.14159265358979 / 180.0, reach = L::kArcRadius + L::kArcWidth / 2;
        const double x = L::kPrimary[L::kBelowGlass].x + std::sin(a) * reach;
        const double y = L::kPrimary[L::kBelowGlass].y - std::cos(a) * reach;
        CHECK(std::hypot(x - L::kGlassX, y - L::kGlassY) >= L::kRingRadius + 2);
    }
    CHECK(L::kStartTop + L::kStartHeight <= L::kInfoTop && L::kInfoTop + L::kInfoHeight <= L::kTopHeight);
    const double track_left = r.start_bar.left + r.track_left, track_right = r.start_bar.left + r.track_right;
    CHECK(std::abs((track_left + track_right) / 2 - L::kWidth / 2) < 1e-9 && track_right - track_left > 600
          && track_right <= r.start_bar.right - L::kThumbRoom);
    CHECK(!overlap(r.file_info, r.status) && r.status.right <= r.version_left);
    CHECK(L::kTopHeight == 624);
    std::printf("round layout: knobs, names, values and scales clear of the ring and of each other\n");
}

// The window of 0.2.1, without a window: the snowflake ends Freeze (the user's wish);
// playhead and grains move on at the display's speeds (0.2.2: MS-R43, MS-R45), the loop
// returns to the core's after a preview (MS-R46); the menus carry
// their ticks (MS-R42) and a menu that could not be shown leaves the frame (MS-R40);
// RESET sets every knob and START to its default and Freeze off (the user's wish).
void TestWindow021() {
    using Clock = MapView::Clock;
    // 0.2.2 (MS-R43, MS-R45): playhead and grains move on at the speeds the display
    // carries, the playhead inside the display's own loop; nothing is kept from the
    // display before. Codex' cases: a 2,000-frame loop at +1x (30 ms: 1,440 frames
    // forward, not 560 back), a 2,000-frame file with a grain at 48,000 frames/s, and a
    // display after a long pause with its own speed.
    {
        auto map = VSTGUI::makeOwned<MapView>(VSTGUI::CRect(0, 0, 840, 342));
        EngineDisplay d;
        d.sequence = 1;
        d.frames = 48000;
        d.loop_start = 1000;
        d.loop_length = 2000;
        d.playhead = 1100;
        d.head_speed = 48000.0f;
        d.rendered = 1;
        d.grain_count = 1;
        d.grains[0] = {100.0f, 0.5f, 0.2f, 48000.0f, 4.0f};
        Clock::time_point t{};
        t += std::chrono::seconds(1);
        map->ShowAt(d, nullptr, t);
        const auto ms = [](int n) { return std::chrono::milliseconds(n); };
        CHECK(map->HeadAt(t) == 1100.0);
        CHECK(std::abs(map->HeadAt(t + ms(30)) - 2540.0) < 1e-6);         // forward, inside the loop
        CHECK(std::abs(map->HeadAt(t + ms(30) * 2) - (1000.0 + 980.0)) < 1e-6); // around it
        CHECK(std::abs(map->HeadAt(t + std::chrono::seconds(5)) - (1000.0 + 1300.0)) < 1e-6); // 0.15 s at most
        CHECK(map->GrainSpeed(0) == 48000.0);
        CHECK(std::abs(map->GrainAt(0, t + ms(30)) - 1540.0) < 1e-6);
        // A file of 2,000 frames: the grain goes round it forward (Codex: -2,000 before).
        d.frames = 2000;
        d.loop_start = 0;
        d.playhead = 100;
        d.rendered = 2;
        map->ShowAt(d, nullptr, t + ms(40));
        CHECK(map->GrainSpeed(0) == 48000.0);
        CHECK(std::abs(map->GrainAt(0, t + ms(80)) - 20.0) < 1e-6); // 100 + 1,920, round the file
        // The same display again after a while: the engine rests, nothing moves on.
        map->ShowAt(d, nullptr, t + ms(200));
        CHECK(map->HeadAt(t + ms(210)) == 100.0 && map->GrainAt(0, t + ms(210)) == 100.0);
        // After a long pause a display with another speed: only its own counts.
        d.rendered = 3;
        d.playhead = 1500;
        d.head_speed = -24000.0f;
        map->ShowAt(d, nullptr, t + ms(1000));
        CHECK(std::abs(map->HeadAt(t + ms(1010)) - 1260.0) < 1e-6);
        // Frozen: the playhead stands where the display has it.
        d.rendered = 4;
        d.frozen = 1;
        d.head_speed = 0.0f;
        map->ShowAt(d, nullptr, t + ms(1100));
        CHECK(map->HeadAt(t + ms(1130)) == 1500.0);
    }
    // MS-R46: the loop from START and SIZE only for a moment after the window's change;
    // then the loop the core reports, also when only the processor got the automation.
    {
        auto map = VSTGUI::makeOwned<MapView>(VSTGUI::CRect(0, 0, 840, 342));
        map->SetStart(0.0);
        map->SetSize(1.0);
        EngineDisplay d;
        d.sequence = 1;
        d.frames = 48000;
        d.loop_start = 24000;
        d.loop_length = 1500;
        d.playhead = 24100;
        map->Show(d, nullptr);
        const MapView::Loop preview = map->ShownLoop(Clock::now());
        CHECK(preview.start == 0.0 && preview.length == 48000.0);
        const MapView::Loop later = map->ShownLoop(Clock::now() + MapView::kPreview + std::chrono::milliseconds(1));
        CHECK(later.start == 24000.0 && later.length == 1500.0);
    }
    // MS-R42: the ticks. Menus need VSTGUI set up (their default font), as the plug-in
    // has it in a host. Showing one needs a real window (MS-R40 is checked in the code).
    VSTGUI::init(GetModuleHandleW(nullptr));
    {
        auto sizes = MakeSizeMenu(1.0);
        CHECK(sizes->isMultipleCheckStyle() && sizes->getNbEntries() == 6);
        for(int32 i = 0; i < 6; ++i)
            CHECK(sizes->getEntry(i)->isChecked() == (i == 2));
        auto free_size = MakeSizeMenu(1.1);
        for(int32 i = 0; i < 6; ++i)
            CHECK(!free_size->getEntry(i)->isChecked());
        auto presets = MakePresetMenu(GuiPreset::kSpiral);
        // 0.2.7: and the two skin entries after a separator.
        CHECK(presets->isMultipleCheckStyle() && presets->getNbEntries() == kGuiPresetCount + 3);
        for(int32 i = 0; i < kGuiPresetCount; ++i) // 0.2.4: all delivered; the spiral checked
            CHECK(presets->getEntry(i)->isChecked() == (i == 4) && presets->getEntry(i)->isEnabled());
        // Round 3: the painted galaxies first, then beta 2's nebula variants.
        CHECK(presets->getEntry(0)->getTitle() == "Blue galaxy" && presets->getEntry(3)->getTitle() == "Violet galaxy");
        CHECK(presets->getEntry(4)->getTitle() == "Spiral galaxy (M51)"
              && presets->getEntry(5)->getTitle() == "Barred spiral (M109)");
        CHECK(presets->getEntry(6)->getTitle() == "Ring nebula (M57)"
              && presets->getEntry(7)->getTitle() == "Diffuse nebula (M42)");
        CHECK(MakePresetMenu(GuiPreset::kRing)->getEntry(6)->isChecked());
        CHECK(MakePresetMenu(GuiPreset::kRedGalaxy)->getEntry(2)->isChecked());
        CHECK(!PopUpMenu(nullptr, sizes, VSTGUI::CPoint(), {})); // nothing to show it in
    }
    VSTGUI::exit();
    // RESET: knobs and START to their defaults, Freeze off; each an edit the host sees.
    {
        IPtr<HostApplication> app = owned(new HostApplication());
        IPtr<Controller> controller = owned(new Controller());
        CHECK(controller->initialize(app) == kResultOk);
        CoalescingHandler handler;
        CHECK(controller->setComponentHandler(&handler) == kResultOk);
        for(const auto& [id, value] : {std::pair{kParamStart, 0.4}, std::pair{kParamSpeed, 0.9},
                                       std::pair{kParamSpray, 0.3}, std::pair{kParamMask, 0.5},
                                       std::pair{kParamFreeze, 1.0}})
            controller->setParamNormalized(id, value);
        IPtr<IPlugView> view = owned(controller->createView(ViewType::kEditor));
        auto* editor = dynamic_cast<Editor*>(view.get());
        CHECK(editor);
        editor->PressReset();
        for(ParamID id : {kParamStart, kParamSpeed, kParamSpray, kParamMask})
            CHECK(controller->getParamNormalized(id) == kDefaults[static_cast<std::size_t>(Index(id))]);
        CHECK(controller->getParamNormalized(kParamFreeze) == 0.0);
        std::vector<ParamID> edited;
        for(int32 i = 0; i < handler.changes.getParameterCount(); ++i)
            edited.push_back(handler.changes.getParameterData(i)->getParameterId());
        std::sort(edited.begin(), edited.end());
        CHECK((edited == std::vector<ParamID>{kParamStart, kParamSpeed, kParamSpray, kParamMask, kParamFreeze}));
        view = nullptr;
        CHECK(controller->terminate() == kResultOk);
    }
    // MS-R44: a change the controller reports asks the processor nothing from inside its
    // call (and a window not open never asks); the controller calls each window from a
    // held copy of its list, only while the window is still registered; a window let go
    // without removed() takes itself out of the list.
    {
        IPtr<HostApplication> app = owned(new HostApplication());
        IPtr<Controller> controller = owned(new Controller());
        CHECK(controller->initialize(app) == kResultOk);
        int polls = 0;
        EditorCallbacks callbacks;
        callbacks.poll = [&] { ++polls; };
        IPtr<Editor> lone(new Editor(controller, EditorSettings{}, std::move(callbacks)), false);
        lone->ParameterChanged(kParamSpeed, 0.3);
        lone->notify(nullptr, VSTGUI::CVSTGUITimer::kMsgTimer);
        CHECK(polls == 0);
        lone = nullptr;
        IPtr<IPlugView> first = owned(controller->createView(ViewType::kEditor));
        IPtr<IPlugView> second = owned(controller->createView(ViewType::kEditor));
        auto* one = dynamic_cast<Editor*>(first.get());
        auto* two = dynamic_cast<Editor*>(second.get());
        CHECK(one && two);
        controller->editorAttached(one);
        controller->editorAttached(two);
        CHECK(controller->OpenEditorCount() == 2);
        std::vector<Editor*> called;
        controller->ForEachEditor([&](Editor& editor) {
            called.push_back(&editor);
            if(&editor == one) { // the host closes and lets go of the other on the way
                controller->editorRemoved(two);
                second = nullptr;
            }
        });
        CHECK(called == std::vector<Editor*>{one});
        CHECK(controller->OpenEditorCount() == 1);
        first = nullptr; // let go while registered
        CHECK(controller->OpenEditorCount() == 0);
        controller->setParamNormalized(kParamSpeed, 0.2); // nothing left to call
        CHECK(controller->terminate() == kResultOk);
    }
    std::printf("window 0.2.1/0.2.2: no request from inside the controller's call, windows from a held list "
                "(MS-R44); playhead and grains at the display's speeds (MS-R43, "
                "MS-R45); the core's loop after a preview (MS-R46); menus with ticks (MS-R42); RESET to "
                "defaults and Freeze off\n");
}

// 0.2.2, the user's design round: the nebula drifts with the signed tempo the display
// carries (forward right, backward left, at rest when frozen, stopped or without tempo,
// never turned round by the playhead's jump at the loop's end), eased; ended grains glow
// on for kTrail, and another file clears them; the layout keeps the window's size.
void TestWindow022() {
    using Clock = MapView::Clock;
    const auto ms = [](int n) { return std::chrono::milliseconds(n); };
    {
        auto map = VSTGUI::makeOwned<MapView>(VSTGUI::CRect(0, 0, 840, 342));
        EngineDisplay d;
        d.sequence = 1;
        d.frames = 96000;
        d.loop_start = 0;
        d.loop_length = 96000;
        d.playhead = 90000;
        d.head_speed = 48000.0f; // 1x forward (no overview: 48 kHz)
        d.rendered = 1;
        Clock::time_point now{};
        now += std::chrono::seconds(1);
        // The window draws about every 20 ms; each frame moves the drift on, and the
        // displays keep coming (0.2.3: without them the drift rests).
        const auto run = [&](int frames) {
            for(int k = 0; k < frames; ++k) {
                now += ms(20);
                ++d.rendered;
                map->ShowAt(d, nullptr, now);
                map->AdvanceDrift(now);
            }
        };
        const auto show = [&](std::uint32_t rendered) {
            d.rendered = rendered;
            map->ShowAt(d, nullptr, now);
        };
        show(1);
        CHECK(std::abs(map->DriftTarget() - 9.0) < 1e-9);
        run(300); // 6 s: eased all the way
        CHECK(std::abs(map->DriftSpeed() - 9.0) < 0.01);
        // The loop turns: the read position jumps back, the tempo stays; so does the drift.
        d.playhead = 1000;
        show(2);
        run(1);
        CHECK(map->DriftTarget() > 0 && map->DriftSpeed() > 8.99);
        // Backward: the drift turns smoothly, not at once.
        d.head_speed = -96000.0f;
        show(3);
        CHECK(std::abs(map->DriftTarget() + 18.0) < 1e-9);
        run(1);
        CHECK(map->DriftSpeed() > 0); // still easing round
        run(300);
        CHECK(std::abs(map->DriftSpeed() + 18.0) < 0.01);
        // A very fast tempo: at most kMaxDrift.
        d.head_speed = 48000.0f * 8;
        show(4);
        CHECK(map->DriftTarget() == 22.0);
        // Frozen (the core reports no tempo then), stopped: at rest.
        d.frozen = 1;
        d.head_speed = 0.0f;
        show(5);
        CHECK(map->DriftTarget() == 0.0);
        d.frozen = 0;
        d.head_speed = 48000.0f;
        show(6);
        map->SetStopped(true);
        CHECK(map->DriftTarget() == 0.0);
        run(300);
        CHECK(map->DriftSpeed() == 0.0);
        map->SetStopped(false);
        CHECK(map->DriftTarget() == 9.0);
        // 0.2.3: no displays any more, and the drift comes to rest.
        run(300);
        for(int k = 0; k < 300; ++k) {
            now += ms(20);
            map->AdvanceDrift(now);
        }
        CHECK(map->DriftSpeed() == 0.0);
        // 0.2.3: a late overview is drawn but is no new display: the head stays at rest.
        const EngineDisplay same = map->Shown();
        auto wave = std::make_shared<WaveOverview>();
        wave->sequence = same.sequence;
        wave->frames = same.frames;
        wave->rate = 48000;
        map->ShowAt(same, wave, now);
        CHECK(map->ShowsWave() && map->HeadAt(now + ms(50)) == same.playhead);
    }
    {
        auto map = VSTGUI::makeOwned<MapView>(VSTGUI::CRect(0, 0, 840, 342));
        EngineDisplay d;
        d.sequence = 1;
        d.frames = 96000;
        d.loop_length = 96000;
        d.rendered = 1;
        d.grain_count = 2;
        d.grains[0] = {1000.0f, 0.2f, 0.9f, 48000.0f, 10.0f}; // ends in 10 ms
        d.grains[1] = {5000.0f, 0.7f, 0.5f, 48000.0f, 2.0f};  // sounds on
        Clock::time_point t{};
        t += std::chrono::seconds(1);
        map->ShowAt(d, nullptr, t);
        CHECK(map->Afterglows(t) == 0);
        // The next display, 30 ms later with 23 quanta (30.7 ms of audio; 0.2.4, MS-R53: the
        // audio's own time): the first grain has ended, the second is still in it.
        EngineDisplay next = d;
        next.rendered = d.rendered + 23;
        next.grain_count = 1;
        next.grains[0] = d.grains[1];
        next.grains[0].phase = 0.56f;
        map->ShowAt(next, nullptr, t + ms(30));
        CHECK(map->Afterglows(t + ms(30)) == 1);
        CHECK(map->Afterglows(t + ms(10 + 449)) == 1); // default: 0.45 s after it ended
        CHECK(map->Afterglows(t + ms(10 + 451)) == 0);
        // 0.2.3 (Codex' check): a running grain with the same pan, speed and window rate
        // but elsewhere in its window does not hide the one that ended.
        auto twin = VSTGUI::makeOwned<MapView>(VSTGUI::CRect(0, 0, 840, 342));
        EngineDisplay a = d;
        a.grains[1] = {8000.0f, 0.2f, 0.2f, 48000.0f, 10.0f};
        twin->ShowAt(a, nullptr, t);
        EngineDisplay b = a;
        b.rendered = a.rendered + 23;
        b.grain_count = 1;
        b.grains[0] = a.grains[1];
        b.grains[0].phase = 0.5f;
        twin->ShowAt(b, nullptr, t + ms(30));
        CHECK(twin->Afterglows(t + ms(30)) == 1);
        // MS-R53 (0.2.4, Codex' case): close phases, 0.90 and 0.85, and a late reply with only
        // 10 quanta (13.3 ms of audio) after 30 ms. The second sounds on near 0.983; the first
        // has ended and glows.
        auto close = VSTGUI::makeOwned<MapView>(VSTGUI::CRect(0, 0, 840, 342));
        EngineDisplay c = d;
        c.grains[0] = {1000.0f, 0.2f, 0.90f, 48000.0f, 10.0f};
        c.grains[1] = {8000.0f, 0.2f, 0.85f, 48000.0f, 10.0f};
        close->ShowAt(c, nullptr, t);
        EngineDisplay e = c;
        e.rendered = c.rendered + 10;
        e.grain_count = 1;
        e.grains[0] = c.grains[1];
        e.grains[0].phase = static_cast<float>(0.85 + 10.0 * 10 * 64 / 48000.0);
        close->ShowAt(e, nullptr, t + ms(30));
        CHECK(close->Afterglows(t + ms(30)) == 1);
        // MS-R54 (0.2.4): about 100 ms left and no display after it; the predicted end fades
        // on the wall clock and is gone after the chosen trail time.
        auto alone = VSTGUI::makeOwned<MapView>(VSTGUI::CRect(0, 0, 840, 342));
        EngineDisplay f = d;
        f.grain_count = 1;
        f.grains[0] = {1000.0f, 0.2f, 0.8f, 48000.0f, 2.0f};
        alone->ShowAt(f, nullptr, t);
        CHECK(alone->EndedFor(0, t + ms(50)) < 0);
        CHECK(std::abs(alone->EndedFor(0, t + ms(350)) - 0.25) < 1e-6);
        CHECK(alone->EndedFor(0, t + ms(450)) < MapView::kTrail);
        CHECK(alone->EndedFor(0, t + ms(560)) >= MapView::kTrail);
        // MS-R57 (0.2.5): 60 ms left, an identical display after 125 ms sets the rest; the
        // end predicted before stays and keeps fading on the wall clock.
        auto rests = VSTGUI::makeOwned<MapView>(VSTGUI::CRect(0, 0, 840, 342));
        EngineDisplay q = d;
        q.grain_count = 1;
        q.grains[0] = {1000.0f, 0.2f, 0.88f, 48000.0f, 2.0f};
        rests->ShowAt(q, nullptr, t);
        rests->ShowAt(q, nullptr, t + ms(125));
        CHECK(std::abs(rests->EndedFor(0, t + ms(130)) - 0.07) < 1e-6);
        CHECK(rests->EndedFor(0, t + ms(60 + 451)) >= MapView::kTrail);
        auto later = VSTGUI::makeOwned<MapView>(VSTGUI::CRect(0, 0, 840, 342));
        q.grains[0].phase = 0.2f; // 0.4 s left: resting, it does not end
        later->ShowAt(q, nullptr, t);
        later->ShowAt(q, nullptr, t + ms(125));
        CHECK(later->EndedFor(0, t + ms(900)) < 0);
        // MS-R55 (0.2.5): the engine's own displays (Codex' case: a mono file of 96,000
        // frames, DENSITY 0.85, OVERLAP 0.55, DISPLACE 0.08, after 111 and 129 quanta).
        // Every grain of the first that is gone from the second and has ended by then
        // glows; the grains carry the number of their start.
        {
            const FileImage image = MakeImage(96000, 48000, 1, 31);
            FileData file;
            file.frames = image.frames;
            file.rate = image.rate;
            file.channels = image.channels;
            file.sequence = 1;
            file.pcm.reset(new std::int16_t[image.pcm.size()]);
            std::copy(image.pcm.begin(), image.pcm.end(), file.pcm.get());
            EngineSettings settings;
            settings.control[static_cast<unsigned>(Control::Density)] = 0.85f;
            settings.control[static_cast<unsigned>(Control::Overlap)] = 0.55f;
            settings.control[static_cast<unsigned>(Control::Displacement)] = 0.08f;
            LooperEngine engine;
            engine.Reset(&file, settings);
            static const float silence[LooperEngine::kQuantum] = {};
            float l[LooperEngine::kQuantum], r[LooperEngine::kQuantum];
            EngineDisplay first, second;
            for(int k = 0; k < 129; ++k) {
                engine.Render(silence, silence, l, r);
                if(k + 1 == 111)
                    engine.Display(first);
            }
            engine.Display(second);
            const double audio = (second.rendered - first.rendered) * LooperEngine::kQuantum / LooperEngine::kSampleRate;
            int ended = 0;
            for(std::uint32_t i = 0; i < first.grain_count; ++i) {
                const GrainDot& g = first.grains[i];
                CHECK(g.id != 0);
                bool there = false;
                for(std::uint32_t k = 0; k < second.grain_count; ++k)
                    there = there || second.grains[k].id == g.id;
                ended += !there && g.phase_rate > 0 && (1.0 - g.phase) / g.phase_rate <= audio ? 1 : 0;
            }
            auto engine_map = VSTGUI::makeOwned<MapView>(VSTGUI::CRect(0, 0, 840, 342));
            engine_map->ShowAt(first, nullptr, t);
            engine_map->ShowAt(second, nullptr, t + ms(24));
            CHECK(ended > 0 && engine_map->Afterglows(t + ms(24)) == static_cast<std::size_t>(ended));
            std::printf("engine displays after 111 and 129 quanta: %d grains ended between them, each glows\n", ended);
        }
        // MS-R56 (0.2.5): START at 100 % of a file of five frames: the core clamps the loop
        // to start at frame 4, the thumb (beta 2: of the START bar) stands there too.
        {
            auto bar = VSTGUI::makeOwned<StartBar>(VSTGUI::CRect(20, 566, 680, 598));
            bar->SetFile(5, 48000.0);
            bar->SetStart(1.0);
            CHECK(std::abs(bar->StartX() - bar->FrameX(4)) < 1e-9 && bar->StartX() < bar->FrameX(5));
        }
        // START (0.2.4): the thumb stands where the loop the core reports begins, at the
        // start, the middle and near the end of the file.
        for(const double start : {0.0, 0.5, 0.98}) {
            auto view = VSTGUI::makeOwned<MapView>(VSTGUI::CRect(0, 0, 436, 436));
            auto bar = VSTGUI::makeOwned<StartBar>(VSTGUI::CRect(20, 566, 680, 598));
            EngineDisplay g = d;
            g.loop_start = start * g.frames;
            view->ShowAt(g, nullptr, t);
            view->SetStart(start);
            bar->SetFile(g.frames, 48000.0);
            bar->SetStart(start);
            CHECK(std::abs(bar->StartX() - bar->FrameX(view->ShownLoop(t + std::chrono::seconds(1)).start)) < 1e-9);
            // Round 2: the end thumb stands where the core's loop ends (START, then SIZE
            // squared of the rest), and a drag to it gives that SIZE back.
            for(const double size : {0.0, 0.3, 0.7, 1.0}) {
                view->SetSize(size);
                bar->SetSize(size);
                const MapView::Loop loop = view->ShownLoop(MapView::Clock::now()); // the preview: START, SIZE
                CHECK(std::abs(bar->EndX() - bar->FrameX(std::min<double>(g.frames, loop.start + loop.length))) < 1e-6);
                if(size > 0.05 && start < 0.9)
                    CHECK(std::abs(bar->SizeAt(bar->EndX()) - size) < 1e-6);
            }
        }
        // Round 2: a press on the end thumb sets LOOP SIZE, one elsewhere START.
        {
            auto bar = VSTGUI::makeOwned<StartBar>(VSTGUI::CRect(20, 566, 680, 598));
            double started = -1, sized = -1;
            StartBar::Edits edits;
            edits.start = [&](double share) { started = share; };
            edits.size = [&](double size) { sized = size; };
            bar->SetEdits(std::move(edits));
            bar->SetFile(48000, 48000.0);
            bar->SetStart(0.2);
            bar->SetSize(0.5); // the loop ends at 0.2 + 0.25 x 0.8 = 0.4 of the file
            CHECK(bar->PressAt(bar->EndX() + 2) && sized >= 0 && started < 0);
            bar->DragTo(bar->FrameX(48000 * 0.6));
            CHECK(std::abs(sized - std::sqrt(0.5)) < 1e-3); // (0.6 - 0.2) / 0.8
            bar->Release();
            sized = -1;
            CHECK(bar->PressAt(bar->FrameX(48000 * 0.8)) && started > 0.79 && sized < 0);
            bar->Release();
        }
        // Another file: the glow is gone at once.
        EngineDisplay other = next;
        other.sequence = 2;
        other.rendered = 3;
        map->ShowAt(other, nullptr, t + ms(60));
        CHECK(map->Afterglows(t + ms(60)) == 0);
    }
    // The layout: STOP and the symbol keys one group with even gaps; KEYS, LATCH, the
    // octave keys and the keyboard one row inside the margins.
    CHECK(layout::kStopRight + layout::kHeaderGap == layout::kSizeLeft
          && layout::kSizeRight + layout::kHeaderGap == layout::kPresetLeft);
    CHECK(layout::kKeysRight + layout::kHeaderGap == layout::kStopLeft);
    CHECK(layout::kLatchLeft >= layout::kMargin && layout::kOctaveUpLeft + layout::kButtonWidth <= layout::kWidth - layout::kMargin);
    TestRoundLayout();
    // Slate: blue-violet controls, orange markers, kept apart.
    const SkinColours& slate = PresetInfo(GuiPreset::kSpiral).colours;
    CHECK(slate.control == VSTGUI::CColor(165, 159, 242) && slate.value == VSTGUI::CColor(200, 197, 254));
    CHECK(slate.marker == VSTGUI::CColor(255, 170, 88)); // 0.2.4: the delivered tables
    // 0.2.3: PITCH tints the grains only away from 0 octaves.
    CHECK(MapView::PitchTint(slate.tail, 0.0) == slate.tail && MapView::PitchTint(slate.tail, -3.0) != slate.tail
          && MapView::PitchTint(slate.tail, 2.0).blue > slate.tail.blue);
    std::printf("window 0.2.2 design: the nebula drifts with the signed tempo, eased, at rest when frozen or "
                "stopped, not turned by a loop's turn; ended grains glow on for %.1f s, a new file clears "
                "them; header group, LATCH/KEYS, the triangle's lane; control and marker colours apart\n",
                MapView::kTrail);
}

// The GUI presets (0.2.0): the bitmaps the window asks for are in the plug-in's
// resource folder (and so in the bundle) with the pixel sizes of the brief, at 1x and
// 2x; presets whose bitmaps are not delivered show as Slate. The window settings carry
// the preset (controller state version 2); a song of version 1 opens with Slate, and a
// preset number not known is refused.
// 0.2.7: custom skins. The first folder wins, file by file; a missing folder, a file
// that is not a PNG, one of the wrong size and a 1x file under a 2x name fall through to
// the next folder and in the end to the built-in bitmap. No image is left without one.
void TestCustomSkin() {
    namespace fs = std::filesystem;
    using namespace VSTGUI;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // WIC reads the PNGs, as in a host
    VSTGUI::init(GetModuleHandleW(nullptr));
    if(auto* win32 = getPlatformFactory().asWin32Factory()) // the built-in bitmaps, as in the bundle
        win32->setResourceBasePath(UTF8String(MESSIER_GUI_DIR "/"));
    const fs::path user = g_dir / "skin user", shared = g_dir / "skin shared", none = g_dir / "skin none";
    fs::create_directories(user);
    fs::create_directories(shared);
    const fs::path gui(MESSIER_GUI_DIR);
    const fs::path fixtures(MESSIER_TEST_SKIN_DIR); // beta 2: images other than the built-in ones
    auto copy = [&](const char* from, const fs::path& to) {
        CHECK(fs::copy_file(fixtures / from, to, fs::copy_options::overwrite_existing));
    };
    copy("messier_plum_knob_s@1x.png", user / "messier_knob_s@1x.png");   // taken
    copy("messier_plum_knob_s@1x.png", user / "messier_knob_l@1x.png");   // 48 x 48: wrong size
    copy("messier_plum_knob_s@1x.png", user / "messier_knob_s@2x.png");   // 1x under the 2x name
    std::ofstream(user / "messier_chassis@1x.png", std::ios::binary) << "not a PNG";
    std::ofstream(user / "messier_glass@1x.png", std::ios::binary); // empty
    copy("messier_plum_knob_l@1x.png", shared / "messier_knob_l@1x.png");   // taken: the user's is wrong
    copy("messier_silver_knob_s@1x.png", shared / "messier_knob_s@1x.png"); // the user's wins
    std::ofstream(user / "messier_slate_colours.txt", std::ios::binary) << "glass = #102030\n";
    std::ofstream(shared / "messier_slate_colours.txt", std::ios::binary) << "glass = #FFFFFF\nmarker = #010203\n";
    // 0.2.8, MS-R60: a PNG is checked by its IHDR before any decoding. The real thumb
    // with an IHDR length of 12, a header declaring 100000 x 100000 pixels, and the real
    // bezel padded past 32 MiB: none reaches the decoder.
    auto read_all = [](const fs::path& file) {
        std::ifstream in(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
    {
        std::string thumb = read_all(gui / "messier_thumb@1x.png");
        std::uint32_t w = 0, h = 0;
        CHECK(SkinPngSize(thumb, w, h) && w == 24 && h == 24);
        thumb[11] = 12;
        CHECK(!SkinPngSize(thumb, w, h));
        std::ofstream(user / "messier_thumb@1x.png", std::ios::binary) << thumb;
        std::string huge = read_all(gui / "messier_button@1x.png");
        for(int at : {16, 20}) {
            huge[at] = 0;
            huge[at + 1] = 1;
            huge[at + 2] = static_cast<char>(0x86);
            huge[at + 3] = static_cast<char>(0xA0); // 100000
        }
        CHECK(SkinPngSize(huge, w, h) && w == 100000 && h == 100000);
        std::ofstream(user / "messier_button@1x.png", std::ios::binary) << huge;
        std::string padded = read_all(gui / "messier_button_down@1x.png");
        padded.resize(kMaxSkinFileBytes + 1, '\0');
        CHECK(SkinPngSize(padded, w, h) && w == 48 && h == 28);
        std::ofstream(user / "messier_button_down@1x.png", std::ios::binary) << padded;
        CHECK(!SkinPngSize("not a PNG", w, h) && !SkinPngSize(std::string(), w, h));
    }
    const IPlatformFactory& factory = getPlatformFactory();
    auto png = [&](CBitmap* bitmap) {
        return bitmap && bitmap->getPlatformBitmap()
                   ? factory.createBitmapMemoryPNGRepresentation(bitmap->getPlatformBitmap())
                   : PNGBitmapBuffer{};
    };
    auto file_png = [&](const char* name) {
        const std::string path = (fixtures / name).string();
        PlatformBitmapPtr bitmap = factory.createBitmapFromPath(path.c_str());
        return bitmap ? factory.createBitmapMemoryPNGRepresentation(bitmap) : PNGBitmapBuffer{};
    };
    // Every image the bundle has (beta 2: under its new name or beta 1's), in both sizes.
    auto complete = [&](const Skin& skin) {
        bool all = true;
        for(std::size_t i = 0; i < kSkinImageCount; ++i) {
            const auto id = static_cast<SkinImageId>(i);
            const std::string legacy = LegacySkinImageFileName(id, 1);
            if(!fs::exists(gui / SkinImageFileName(skin.Preset(), id, 1)) && (legacy.empty() || !fs::exists(gui / legacy)))
                continue; // not delivered: drawn by code or left out
            const SkinImage& image = skin.Image(id);
            // MS-R70: the dust masks at 1x only.
            const bool dust = id == SkinImageId::kDustWisps || id == SkinImageId::kDustVeil
                              || id == SkinImageId::kDustBands || id == SkinImageId::kDustClouds;
            all = all && image.x1 && (dust ? !image.x2 : static_cast<bool>(image.x2))
                  && image.x1->getWidth() == image.size.x && image.x1->getHeight() == image.size.y;
        }
        return all;
    };
    const SkinColours& gold = PresetInfo(GuiPreset::kCloud).colours;
    {
    const Skin plain(GuiPreset::kCloud, {none});
    CHECK(plain.Overrides() == 0 && complete(plain) && plain.Colours().glass == gold.glass);
    const Skin built_in(GuiPreset::kCloud, {});
    CHECK(png(plain.Image(SkinImageId::kChassis).x1) == png(built_in.Image(SkinImageId::kChassis).x1));
    const int decoded_before = SkinFilesDecoded();
    // B2C.3 (the user's decision): beta 1's colour table only under the spiral, Slate's successor.
    const Skin custom(GuiPreset::kSpiral, {user, none, shared});
    CHECK(custom.Overrides() == 2 + 3 && complete(custom)); // two bitmaps, three colour values
    CHECK(SkinFilesDecoded() - decoded_before == 2); // only the two that are taken were decoded
    {
        const Skin other(GuiPreset::kCloud, {user, none, shared});
        CHECK(other.Overrides() == 2 && other.Colours().glass == gold.glass); // the table left out
    }
    for(const SkinImageId id : {SkinImageId::kThumb, SkinImageId::kButton, SkinImageId::kButtonDown})
        CHECK(png(custom.Image(id).x1) == png(built_in.Image(id).x1));
    // MS-R61: a colour table of more than 64 KiB is refused whole; one of exactly 64 KiB
    // is read.
    {
        const fs::path big = g_dir / "skin big";
        fs::create_directories(big);
        std::string table = "marker = #040506\n";
        table.resize(kMaxSkinTableBytes, ';');
        std::ofstream(big / "messier_slate_colours.txt", std::ios::binary) << table;
        const Skin exact(GuiPreset::kSpiral, {big});
        CHECK(exact.Overrides() == 1 && exact.Colours().marker == CColor(4, 5, 6));
        std::ofstream(big / "messier_slate_colours.txt", std::ios::binary) << table << ';';
        const Skin larger(GuiPreset::kSpiral, {big});
        CHECK(larger.Overrides() == 0 && larger.Colours().marker == gold.marker);
    }
    // C7 (0.2.9): a PNG with a valid header but its pixel data zeroed is decoded once in
    // full and, failing, gives the built-in bitmap. (WIC decodes a cut-off or scrambled
    // file without an error, partly; such a file draws as far as it decodes.)
    {
        const fs::path cut = g_dir / "skin cut";
        fs::create_directories(cut);
        // Beta 2: from the image data's chunk on (the delivered PNGs have one image chunk, in
        // which zeros alone leave WIC a partial picture without an error).
        std::string keys = read_all(gui / "messier_chassis@1x.png");
        const std::size_t data = keys.find("IDAT");
        CHECK(data != std::string::npos && data > 33);
        for(std::size_t i = data; i < keys.size() * 3 / 4; ++i)
            keys[i] = 0;
        std::ofstream(cut / "messier_chassis@1x.png", std::ios::binary) << keys;
        const int before = SkinFilesDecoded();
        const Skin broken(GuiPreset::kCloud, {cut});
        CHECK(SkinFilesDecoded() - before == 1 && broken.Overrides() == 0);
        CHECK(png(broken.Image(SkinImageId::kChassis).x1) == png(built_in.Image(SkinImageId::kChassis).x1));
    }
    CHECK(custom.Colours().glass == CColor(16, 32, 48) && custom.Colours().marker == CColor(1, 2, 3));
    const PNGBitmapBuffer plum_small = file_png("messier_plum_knob_s@1x.png");
    CHECK(!plum_small.empty() && png(custom.Image(SkinImageId::kKnobSmall).x1) == plum_small);
    CHECK(png(custom.Image(SkinImageId::kKnobLarge).x1) == file_png("messier_plum_knob_l@1x.png"));
    for(const SkinImageId id : {SkinImageId::kChassis, SkinImageId::kKeyWhite})
        CHECK(png(custom.Image(id).x1) == png(built_in.Image(id).x1));
    CHECK(png(custom.Image(SkinImageId::kKnobSmall).x2) == png(built_in.Image(SkinImageId::kKnobSmall).x2));
    } // the skins go before VSTGUI does
    VSTGUI::exit();
    if(SUCCEEDED(com))
        CoUninitialize();
    std::printf("custom skins: the first folder wins file by file; a missing folder, a broken, an empty, a "
                "wrong-sized and a wrong-scale file give the built-in bitmap\n");
}

void TestGuiPresets() {
    // Beta 2: every bitmap the bundle has, under its new name or beta 1's, at its size; the
    // ones the window cannot do without are there under one of them (until the delivery of
    // docs/GRAPHICS_BRIEF_BETA2.md, beta 1's of Slate).
    const auto size_of = [](const std::filesystem::path& file, std::uint32_t& w, std::uint32_t& h) {
        std::ifstream in(file, std::ios::binary);
        unsigned char head[24] = {};
        in.read(reinterpret_cast<char*>(head), sizeof head);
        if(in.gcount() != 24 || std::memcmp(head + 12, "IHDR", 4) != 0)
            return false;
        const auto be = [&](int at) {
            return (std::uint32_t(head[at]) << 24) | (std::uint32_t(head[at + 1]) << 16)
                   | (std::uint32_t(head[at + 2]) << 8) | std::uint32_t(head[at + 3]);
        };
        w = be(16);
        h = be(20);
        return true;
    };
    int found = 0;
    for(std::int32_t p = 0; p < kGuiPresetCount; ++p)
        for(std::size_t i = 0; i < kSkinImageCount; ++i) {
            const auto id = static_cast<SkinImageId>(i);
            bool any = false;
            for(const bool legacy : {false, true}) {
                bool both = true;
                for(int scale : {1, 2}) {
                    const std::string name = legacy ? LegacySkinImageFileName(id, scale)
                                                    : SkinImageFileName(static_cast<GuiPreset>(p), id, scale);
                    const std::filesystem::path file = std::filesystem::path(MESSIER_GUI_DIR) / name;
                    std::uint32_t w = 0, h = 0;
                    if(name.empty() || !std::filesystem::exists(file)) {
                        both = false;
                        continue;
                    }
                    const VSTGUI::CPoint size = legacy ? LegacySkinImageSize(id) : SkinImageSize(id);
                    CHECK(size_of(file, w, h) && w == std::uint32_t(size.x) * scale && h == std::uint32_t(size.y) * scale);
                }
                any = any || both;
            }
            const bool needed = id == SkinImageId::kChassis || id == SkinImageId::kKnobLarge || id == SkinImageId::kKnobSmall
                                || id == SkinImageId::kThumb || id == SkinImageId::kButton
                                || id == SkinImageId::kButtonDown || id == SkinImageId::kKeyWhite
                                || id == SkinImageId::kKeyBlack;
            CHECK(!needed || any);
            found += any ? 1 : 0;
        }
    CHECK(found >= 4 * 8);
    CHECK(SkinImageFileName(GuiPreset::kCloud, SkinImageId::kChassis, 2) == "messier_chassis@2x.png");
    CHECK(SkinImageFileName(GuiPreset::kRing, SkinImageId::kNebulaArms, 1) == "messier_nebula_ring_arms@1x.png");
    CHECK(LegacySkinImageFileName(SkinImageId::kThumb, 2) == "messier_slate_thumb@2x.png"
          && LegacySkinImageSize(SkinImageId::kThumb) == VSTGUI::CPoint(16, 28)
          && LegacySkinImageFileName(SkinImageId::kRing, 1).empty());
    // 0.2.6: a colour table written as text reads back the same; unknown lines and
    // comments are left alone; a skin folder only in a skin test build.
    for(std::int32_t p = 0; p < kGuiPresetCount; ++p) {
        const SkinColours& colours = PresetInfo(static_cast<GuiPreset>(p)).colours;
        SkinColours read = PresetInfo(GuiPreset::kSpiral).colours;
        read.negative = !colours.negative;
        CHECK(ApplyColourTable(ColourTableText(colours), read) == 33); // beta 2: start_fill
        CHECK(ColourTableText(read) == ColourTableText(colours) && read.negative == colours.negative);
    }
    {
        SkinColours some = PresetInfo(GuiPreset::kCloud).colours;
        CHECK(ApplyColourTable("; a comment\nglass = #102030 ; dark\nnothing = #FFFFFF\nmarker = red\n", some) == 1);
        CHECK(some.glass == VSTGUI::CColor(16, 32, 48) && some.marker == PresetInfo(GuiPreset::kCloud).colours.marker);
        // C9 (0.2.9): a byte-order mark before the first line.
        CHECK(ApplyColourTable("\xEF\xBB\xBFmarker = #010203\n", some) == 1 && some.marker == VSTGUI::CColor(1, 2, 3));
    }
    // 0.2.7: custom skins, the user's folder in Documents first; the guide names every
    // bitmap with its size and every colour.
    CHECK(UserSkinFolder().filename() == "Messier-Skin" && BundleSkinFolder().filename() == "Messier-Skin"
          && UserSkinFolder() != BundleSkinFolder());
    {
        const std::string guide = SkinFolderGuide();
        // Beta 2: the new names and sizes, the nebula layers per variant.
        CHECK(guide.find("messier_chassis@1x.png") != std::string::npos && guide.find("880 x 804") != std::string::npos
              && guide.find("1760 x 1608") != std::string::npos && guide.find("messier_keys_black@1x.png") != std::string::npos
              && guide.find("messier_nebula_<nebula>_arms@1x.png") != std::string::npos);
        // B2C.2: the painted galaxy's name as the skin looks for it, and every preset's key.
        CHECK(guide.find("messier_nebula_<nebula>@1x.png") != std::string::npos
              && guide.find("_@1x.png") == std::string::npos);
        for(std::int32_t p = 0; p < kGuiPresetCount; ++p)
            CHECK(guide.find(std::string(" ") + PresetInfo(static_cast<GuiPreset>(p)).nebula_key) != std::string::npos);
        std::istringstream table(ColourTableText(PresetInfo(GuiPreset::kSpiral).colours));
        std::string line;
        while(std::getline(table, line))
            CHECK(guide.find("  " + line.substr(0, line.find(' ')) + "\n") != std::string::npos
                  || line.rfind("negative", 0) == 0);
    }
    for(std::int32_t p = 0; p < kGuiPresetCount; ++p) // 0.2.4: all delivered
        CHECK(PresetInfo(static_cast<GuiPreset>(p)).delivered && ShownPreset(static_cast<GuiPreset>(p)) == static_cast<GuiPreset>(p));
    CHECK(ShownPreset(static_cast<GuiPreset>(9)) == GuiPreset::kSpiral);
    // The seconds axis: labels at least 64 units apart on the 840 of the plate.
    CHECK(layout::AxisStep(8.0, 840) == 1 && layout::AxisStep(0.5, 840) == 0.05 && layout::AxisStep(600, 840) == 60);
    CHECK(layout::AxisLabel(1.5, 0.5) == "1.5 s" && layout::AxisLabel(0.25, 0.25) == "0.25 s"
          && layout::AxisLabel(90, 60) == "1:30" && layout::AxisLabel(3, 1) == "3 s");

    IPtr<HostApplication> app = owned(new HostApplication());
    IPtr<Controller> controller = owned(new Controller());
    CHECK(controller->initialize(app) == kResultOk);
    auto load = [&](std::int32_t version, double zoom, std::int8_t keys, std::int32_t transpose, const std::int32_t* preset,
                    std::int8_t details = -1, const std::vector<std::int32_t>* accents = nullptr,
                    std::int32_t tail = -1, std::int8_t galaxy = -1) {
        MemoryStream stream;
        int32 written = 0;
        stream.write(&version, 4, &written);
        stream.write(&zoom, 8, &written);
        stream.write(&keys, 1, &written);
        stream.write(&transpose, 4, &written);
        if(preset)
            stream.write(const_cast<std::int32_t*>(preset), 4, &written);
        if(details >= 0)
            stream.write(&details, 1, &written);
        if(accents) {
            if(version >= 9) { // round 3: their count first
                std::int32_t count = static_cast<std::int32_t>(accents->size());
                stream.write(&count, 4, &written);
            }
            for(std::int32_t hue : *accents)
                stream.write(&hue, 4, &written);
        }
        if(tail >= 0) // version 5: trail time; versions 6 to 8: WAVE setting
            stream.write(&tail, 4, &written);
        if(galaxy >= 0) // version 8 (beta 2): GALAXY or WAVE
            stream.write(&galaxy, 1, &written);
        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        return controller->setState(&stream);
    };
    const std::int32_t plum = 2, unknown = kGuiPresetCount;
    CHECK(load(2, 1.25, 0, 12, &plum) == kResultOk);
    CHECK(controller->WindowSettings().preset == GuiPreset::kRing && controller->WindowSettings().zoom == 1.25);
    CHECK(controller->WindowSettings().wave_percent == kPresetWave && controller->WindowSettings().WavePercent() == 100);
    MemoryStream own;
    CHECK(controller->getState(&own) == kResultOk);
    int64 length = 0;
    own.seek(0, IBStream::kIBSeekEnd, &length);
    CHECK(length == 43 + 4 + 4 * (kGuiPresetCount - kLegacyGuiPresetCount)); // version 9: the accents' count
    CHECK(controller->WindowSettings().galaxy); // a song before beta 2: GALAXY
    CHECK(!controller->WindowSettings().details); // a song of 0.2.3: DETAILS closed
    CHECK(load(3, 1.0, 1, 0, &plum, 1) == kResultOk && controller->WindowSettings().details);
    CHECK(load(1, 1.0, 1, -12, nullptr) == kResultOk); // a song of 0.1.x
    CHECK(controller->WindowSettings().preset == GuiPreset::kSpiral && controller->WindowSettings().keyboard);
    CHECK(controller->WindowSettings().WavePercent() == 100);
    CHECK(load(2, 1.0, 1, 0, &unknown) == kResultFalse);
    CHECK(load(2, 1.0, 1, 0, nullptr) == kResultFalse); // version 2 without its preset
    CHECK(load(3, 1.0, 1, 0, &plum) == kResultFalse); // version 3 without DETAILS
    CHECK(load(4, 1.0, 1, 0, &plum, 0) == kResultFalse);
    CHECK(controller->WindowSettings().preset == GuiPreset::kSpiral);
    // Up to version 8: the hues of the four presets of then; the newer ones keep their own.
    const std::vector<std::int32_t> accents{0, kPresetAccent, 120, kAccentMaxHue};
    const auto has = [&](const std::vector<std::int32_t>& hues) {
        const auto& kept = controller->WindowSettings().accents;
        for(std::size_t i = 0; i < kept.size(); ++i)
            if(kept[i] != (i < hues.size() ? hues[i] : kPresetAccent))
                return false;
        return true;
    };
    CHECK(load(4, 1.0, 1, 0, &plum, 0, &accents) == kResultOk);
    CHECK(has(accents) && controller->WindowSettings().WavePercent() == 100);
    auto invalid = accents;
    invalid.back() = kAccentMaxHue + 1;
    CHECK(load(4, 1.0, 1, 0, &plum, 1, &invalid) == kResultFalse);
    CHECK(has(accents) && !controller->WindowSettings().details);
    // Version 5 (0.2.5): its trail is read and checked, no longer used (0.2.6: fixed).
    CHECK(load(5, 1.0, 1, 0, &plum, 1, &accents) == kResultFalse);
    CHECK(load(5, 1.0, 1, 0, &plum, 1, &accents, kTrailMaxMs + 1) == kResultFalse);
    CHECK(load(5, 1.0, 1, 0, &plum, 1, &accents, 720) == kResultOk);
    CHECK(controller->WindowSettings().details && controller->WindowSettings().WavePercent() == 100);
    // Version 6 (0.2.6): WAVE from 25 to 200 %.
    CHECK(load(6, 1.0, 1, 0, &plum, 1, &accents) == kResultFalse);
    CHECK(load(6, 1.0, 1, 0, &plum, 1, &accents, kWaveMaxPercent + 1) == kResultFalse);
    CHECK(load(6, 1.0, 1, 0, &plum, 1, &accents, kWaveMinPercent - 1) == kResultFalse);
    CHECK(load(6, 1.0, 1, 0, &plum, 1, &accents, kPresetWave) == kResultFalse);
    // Version 8 (beta 2): GALAXY (1) or WAVE (0), nothing else, and nothing missing.
    CHECK(load(8, 1.0, 1, 0, &plum, 1, &accents, kWaveDefaultPercent) == kResultFalse);
    CHECK(load(8, 1.0, 1, 0, &plum, 1, &accents, kPresetWave, 2) == kResultFalse);
    CHECK(load(8, 1.0, 1, 0, &plum, 1, &accents, kPresetWave, 0) == kResultOk);
    CHECK(!controller->WindowSettings().galaxy);
    {
        MemoryStream wave;
        CHECK(controller->getState(&wave) == kResultOk);
        CHECK(load(7, 1.0, 1, 0, &plum, 1, &accents, kPresetWave) == kResultOk
              && controller->WindowSettings().galaxy);
        wave.seek(0, IBStream::kIBSeekSet, nullptr);
        CHECK(controller->setState(&wave) == kResultOk && !controller->WindowSettings().galaxy);
    }
    // Version 9 (round 3): the accents' count first; hues of presets unknown here are read
    // and dropped, a count out of range or a missing hue refuses the state.
    {
        std::vector<std::int32_t> all(kGuiPresetCount, kPresetAccent);
        all[kGuiPresetCount - 1] = 240;
        CHECK(load(9, 1.0, 1, 0, &plum, 1, &all, kPresetWave, 1) == kResultOk && has(all));
        std::vector<std::int32_t> more = all;
        more.push_back(17); // a later version's preset
        CHECK(load(9, 1.0, 1, 0, &plum, 1, &more, kPresetWave, 0) == kResultOk && has(all)
              && !controller->WindowSettings().galaxy);
        const std::vector<std::int32_t> two{10, 20};
        CHECK(load(9, 1.0, 1, 0, &plum, 1, &two, kPresetWave, 1) == kResultOk && has(two));
        std::vector<std::int32_t> too_many(kMaxStoredAccents + 1, kPresetAccent);
        CHECK(load(9, 1.0, 1, 0, &plum, 1, &too_many, kPresetWave, 1) == kResultFalse);
        const std::int32_t galaxy = static_cast<std::int32_t>(GuiPreset::kVioletGalaxy);
        CHECK(load(9, 1.0, 1, 0, &galaxy, 1, &all, kPresetWave, 1) == kResultOk
              && controller->WindowSettings().preset == GuiPreset::kVioletGalaxy);
        // A preset of a later version is refused, as before.
        const std::int32_t later = kGuiPresetCount;
        CHECK(load(9, 1.0, 1, 0, &later, 1, &all, kPresetWave, 1) == kResultFalse);
    }
    CHECK(load(10, 1.0, 1, 0, &plum, 1, &accents, kPresetWave, 1) == kResultFalse);
    // MS-R62: a zoom out of range refuses the whole state, as every other field does.
    CHECK(load(6, 2.5, 1, 0, &plum, 1, &accents, 100) == kResultFalse);
    CHECK(load(6, 0.25, 1, 0, &plum, 1, &accents, 100) == kResultFalse);
    CHECK(controller->WindowSettings().WavePercent() == 100 && controller->WindowSettings().zoom == 1.0);
    CHECK(load(6, 2.0, 1, 0, &plum, 1, &accents, 100) == kResultOk && controller->WindowSettings().zoom == 2.0);
    CHECK(load(6, 0.5, 1, 0, &plum, 1, &accents, 100) == kResultOk && controller->WindowSettings().zoom == 0.5);
    CHECK(load(6, 1.0, 1, 0, &plum, 1, &accents, 100) == kResultOk);
    CHECK(controller->WindowSettings().WavePercent() == 100);
    own.seek(0, IBStream::kIBSeekSet, nullptr);
    CHECK(controller->setState(&own) == kResultOk);
    CHECK(controller->WindowSettings().wave_percent == kPresetWave && controller->WindowSettings().WavePercent() == 100);
    CHECK(load(6, 1.0, 1, 0, &plum, 1, &accents, 160) == kResultOk);
    MemoryStream with_wave;
    CHECK(controller->getState(&with_wave) == kResultOk);
    CHECK(load(3, 1.0, 1, 0, &plum, 0) == kResultOk);
    CHECK(controller->WindowSettings().Accent() == kPresetAccent);
    CHECK(controller->WindowSettings().wave_percent == kPresetWave);
    with_wave.seek(0, IBStream::kIBSeekSet, nullptr);
    CHECK(controller->setState(&with_wave) == kResultOk && has(accents));
    CHECK(controller->WindowSettings().wave_percent == 160);
    {
        IPtr<IPlugView> view = owned(controller->createView(ViewType::kEditor));
        auto* editor = dynamic_cast<Editor*>(view.get());
        CHECK(editor && editor->Settings().WavePercent() == 160);
        editor->SetGuiPreset(GuiPreset::kCloud);
        CHECK(editor->Settings().WavePercent() == 160);
        editor->SetWave(kPresetWave);
        CHECK(editor->Settings().WavePercent() == 100);
        editor->SetGuiPreset(GuiPreset::kRing); // beta 2: no preset has its own WAVE any more
        CHECK(editor->Settings().WavePercent() == 100);
        editor->SetGuiPreset(GuiPreset::kSpiral);
        CHECK(editor->Settings().WavePercent() == 100);
        // Round 6 (the user's test): a preset chosen comes in its own colours, also the same
        // one again after a turn of HUE.
        editor->SetAccent(0); // red
        CHECK(editor->Settings().Accent() == 0);
        editor->SetGuiPreset(GuiPreset::kOrangeGalaxy);
        CHECK(editor->Settings().Accent() == kPresetAccent);
        editor->SetAccent(0);
        editor->SetGuiPreset(GuiPreset::kOrangeGalaxy);
        CHECK(editor->Settings().Accent() == kPresetAccent && editor->Settings().preset == GuiPreset::kOrangeGalaxy);
    }
    // F3 of the Fable audit (0.2.9): a change in one window reaches the other window of
    // the instance; a view kept while closed takes the controller's settings when asked.
    {
        IPtr<IPlugView> a = owned(controller->createView(ViewType::kEditor));
        IPtr<IPlugView> b = owned(controller->createView(ViewType::kEditor));
        auto* one = dynamic_cast<Editor*>(a.get());
        auto* two = dynamic_cast<Editor*>(b.get());
        CHECK(one && two);
        controller->editorAttached(one);
        controller->editorAttached(two);
        one->SetGuiPreset(GuiPreset::kCloud);
        CHECK(controller->WindowSettings().preset == GuiPreset::kCloud && two->Settings().preset == GuiPreset::kCloud);
        controller->editorRemoved(one);
        controller->editorRemoved(two);
        CHECK(load(6, 1.5, 0, 0, &plum, 1, &accents, 100) == kResultOk);
        CHECK(two->Settings().preset == GuiPreset::kCloud); // closed: not told
        ViewRect size;
        CHECK(two->getSize(&size) == kResultOk && two->Settings().preset == GuiPreset::kRing
              && two->Settings().zoom == 1.5 && !two->Settings().keyboard);
        // C3 (0.2.9): the menus' controls (tag -1) do not reach the host as an edit.
        CountingHandler handler;
        CHECK(controller->setComponentHandler(&handler) == kResultOk);
        auto* base = static_cast<VSTGUI::VSTGUIEditorInterface*>(one);
        base->beginEdit(-1);
        base->endEdit(-1);
        CHECK(handler.begins == 0 && handler.ends == 0);
        base->beginEdit(static_cast<int32_t>(kParamVolume));
        base->endEdit(static_cast<int32_t>(kParamVolume));
        CHECK(handler.begins == 1 && handler.ends == 1);
        CHECK(controller->setComponentHandler(nullptr) == kResultOk);
    }
    CHECK(controller->terminate() == kResultOk);
    std::printf("gui presets: the bitmaps of all four presets in the resources at the brief's sizes; the "
                "preset, DETAILS, accent hues and WAVE saved with the window (state 7); states 1 to 6 read\n");
}

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
    // F1 of the Fable audit (0.2.9): the refusal is a failed request for the project's
    // file, which the controller takes (no window load came before it).
    {
        const FileStatus refusal = host.processor->Loader().Status();
        CHECK(refusal.request_state == RequestState::kFailed && refusal.request == g_path_mono
              && refusal.HasItsTexts());
        FileStatus nameless = refusal;
        nameless.request.clear(); // no memory left even for the name
        CHECK(nameless.HasItsTexts());
    }
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
    // E3 (0.2.9): the failure reported before is no longer on the file line.
    CHECK(host.processor->Loader().Status().request_state == RequestState::kNone
          && host.processor->Loader().Status().detail.empty());
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
    // 0.2.2: the speeds must be numbers in range.
    CHECK(refused([](EngineDisplay& d) { d.head_speed = std::numeric_limits<float>::quiet_NaN(); }));
    CHECK(refused([](EngineDisplay& d) { d.grains[0].speed = std::numeric_limits<float>::infinity(); }));
    CHECK(refused([](EngineDisplay& d) { d.grains[0].phase_rate = -1.0f; }));
    CHECK(refused([](EngineDisplay& d) { d.grains[0].phase_rate = std::numeric_limits<float>::min(); })); // 0.2.3
    CHECK(!refused([](EngineDisplay& d) { d.head_speed = -192000.0f; }));
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
    wave->rate = 44100; // 0.2.0: the file's rate for the seconds axis
    CHECK(ReadOverviewBytes(wave.get(), sizeof(WaveOverview), good) != nullptr);
    wave->rate = 5;
    CHECK(!ReadOverviewBytes(wave.get(), sizeof(WaveOverview), good));
    wave->rate = 0;
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
    FakePackage dropped_wav;
    dropped_wav.items = {{VSTGUI::IDataPackage::kFilePath, g_path}};
    auto drop_target = VSTGUI::makeOwned<FileDropTarget>([](FileDropTarget::Hover, const std::string&) {},
                                                         [&](const std::string& path) { controller->LoadFile(path); });
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
        // MS-R37: drag and drop, with the editor's own drop callback (a load through the
        // controller). Entered first, and dropped without an enter.
        {"drop target: enter and drop", [&] {
             const VSTGUI::DragEventData data{&dropped_wav, VSTGUI::CPoint(), VSTGUI::Modifiers()};
             if(drop_target->onDragEnter(data) != VSTGUI::DragOperation::Copy) {
                 drop_target->onDragLeave(data);
                 return static_cast<tresult>(kOutOfMemory);
             }
             return static_cast<tresult>(drop_target->onDrop(data) ? kResultOk : kOutOfMemory);
         }},
        {"drop target: drop without enter", [&] {
             const VSTGUI::DragEventData data{&dropped_wav, VSTGUI::CPoint(), VSTGUI::Modifiers()};
             return static_cast<tresult>(drop_target->onDrop(data) ? kResultOk : kOutOfMemory);
         }},
        // MS-R37: the editor without a window, as a host may call it: sizes, the scale
        // factor and the timer (which asks the controller for status and display).
        {"editor: sizes, scale and timer without a window", [&] {
             ViewRect size(0, 0, 900, 700);
             editor->getSize(&size);
             editor->checkSizeConstraint(&size);
             editor->setContentScaleFactor(1.5f);
             editor->notify(nullptr, VSTGUI::CVSTGUITimer::kMsgTimer);
             return static_cast<tresult>(kResultOk);
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
    std::printf("host calls: no exception leaves them, whichever allocation fails (nineteen calls, "
                "parameter texts, the window, drag and drop, the display, the factory and the life "
                "cycle included)\n");
#else
    std::printf("host calls under failing allocations: Release builds only (Debug STL proxies)\n");
#endif
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    UseSkinFolders({}); // 0.2.7: the built-in skin, whatever custom skin this computer holds
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
    TestLens();
    TestStarfield();
    TestStopAndBypass();
    TestSwitchesOnTheSample();
    TestScreenAndHostKeys();
    TestLongPause();
    TestDialogStart();
    TestResizeGrip();
    TestGuiPresets();
    TestCustomSkin();
    TestHueScale(); // a Skin needs VSTGUI set up, as TestGuiPresets does
    TestWindow021();
    TestWindow022();
    TestState();
    TestExportStart();
    TestMessagesAndKeyboard();
    TestHostWithoutEmptyTexts();
    TestStatusChecks();
    TestCancelledRead();
    TestSwapWhileResting();
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
