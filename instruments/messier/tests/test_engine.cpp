// Tests of the looper engine (layer 2):
//   * bit for bit against the separately compiled firmware engine
//     (tests/reference/, driven by reference_api.cpp, not by the plug-in's code):
//     every pot and secondary moving, notes on several channels, the wheel,
//     Freeze, Reset, mono and stereo files at 44.1 and 48 kHz, a file change;
//   * the level of one grain: 0.90 of the sample (F42), so that the patch cannot
//     get lost in a later update of the core;
//   * the settings of the core (vocoder on the common grid);
//   * two instances do not disturb each other, also in two threads;
//   * a file swap while playing: no click above the fade, no allocation;
//   * Volume behind the core, exact at 100 %;
//   * the display laws in parameters.h against the core's functions;
//   * the window's display (0.1.4): reading it changes nothing in the sound, it
//     shows the loop as the core applies it, the playhead inside it, the sounding
//     grains, and the file the core reads, also through a swap.
#define TEST_SUPPORT_COUNT_ALLOCATIONS
#include "test_support.h"

#include "looper_engine.h"
#include "parameters.h"
#include "reference_api.h"
#include "reference_dsp.h" // the plug-in's core, for the laws

#include <xmmintrin.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

using namespace messier;
using namespace test_support;
using messier_reference::FileImage;
using messier_reference::FirmwareLooper;

namespace {

constexpr int kQ = LooperEngine::kQuantum;

// --- Test material -------------------------------------------------------

FileImage MakeImage(std::uint32_t frames, std::uint32_t rate, std::uint16_t channels,
                    std::uint32_t seed) {
    FileImage image;
    image.frames = frames;
    image.rate = rate;
    image.channels = channels;
    image.pcm.resize(static_cast<std::size_t>(frames) * channels);
    Lcg random(seed);
    double phase[2] = {0, 0};
    for(std::uint32_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / rate;
        const double hit = std::fmod(t, 0.37);
        for(unsigned c = 0; c < channels; ++c) {
            phase[c] += 2.0 * kPi * (c ? 277.2 : 220.0) / rate;
            const double noise = (random.Unit() - 0.5) * 0.05;
            const double x = 0.3 * std::sin(phase[c]) + 0.1 * std::sin(3.1 * phase[c] + c)
                             + 0.4 * std::exp(-hit * 40.0) * std::sin(2.0 * kPi * 900.0 * hit) + noise;
            image.pcm[static_cast<std::size_t>(i) * channels + c] =
                static_cast<std::int16_t>(std::lround(std::clamp(x, -1.0, 1.0) * 32767.0));
        }
    }
    return image;
}

std::unique_ptr<FileData> ToFile(const FileImage& image) {
    auto file = std::make_unique<FileData>();
    file->frames = image.frames;
    file->rate = image.rate;
    file->channels = image.channels;
    file->pcm.reset(new std::int16_t[image.pcm.size()]);
    std::copy(image.pcm.begin(), image.pcm.end(), file->pcm.get());
    return file;
}

// --- One score, two ways to play it --------------------------------------

struct Action {
    enum Kind { kPrimary, kSecondary, kNoteOn, kNoteOff, kAllOff, kBend, kFreeze, kReset, kSwap };
    std::int64_t quantum;
    Kind kind;
    unsigned a = 0, b = 0;
    float value = 0.0f;
};
using Score = std::vector<Action>;

// Every pot and every secondary that acts without an input moves; notes come and
// go on four channels; the wheel, Freeze and Reset now and then. `swap_at`: a file
// change at that quantum (to file 1), -1 for none.
Score MakeScore(std::int64_t quanta, std::uint32_t seed, std::int64_t swap_at = -1) {
    Score score;
    Lcg random(seed);
    static constexpr unsigned kSecondaries[] = {0, 2, 3, 4, 5, 7};
    unsigned held[4] = {128, 128, 128, 128};
    for(std::int64_t q = 2; q < quanta; ++q) {
        if(q == swap_at)
            score.push_back({q, Action::kSwap});
        const std::uint32_t roll = random.Below(100);
        if(roll < 18) {
            float v = random.Unit();
            const unsigned pot = random.Below(8);
            if(pot == 3)
                v = 0.2f + 0.6f * v; // Density: not every test second at 983 grains/s
            score.push_back({q, Action::kPrimary, pot, 0, v});
        } else if(roll < 26) {
            const unsigned which = kSecondaries[random.Below(6)];
            // Displacement above 0.3 empties the grain pool (F41); keep it audible.
            const float v = which == 3 ? 0.3f * random.Unit() : random.Unit();
            score.push_back({q, Action::kSecondary, which, 0, v});
        } else if(roll < 31) {
            const unsigned channel = random.Below(4);
            const unsigned note = 36 + random.Below(60);
            held[channel] = note;
            score.push_back({q, Action::kNoteOn, channel, note, 0.0f});
            score.back().value = static_cast<float>(1 + random.Below(127));
        } else if(roll < 35) {
            const unsigned channel = random.Below(4);
            if(held[channel] < 128)
                score.push_back({q, Action::kNoteOff, channel, held[channel]});
            held[channel] = 128;
        } else if(roll < 37) {
            score.push_back({q, Action::kBend, random.Below(16384)});
        } else if(roll == 37) {
            score.push_back({q, Action::kFreeze, random.Below(2)});
        } else if(roll == 38) {
            score.push_back({q, Action::kReset});
        } else if(roll == 39 && random.Below(8) == 0) {
            score.push_back({q, Action::kAllOff});
        }
    }
    return score;
}

void ApplyToPlugin(LooperEngine& engine, const Action& a, const FileData* swap_to) {
    switch(a.kind) {
        case Action::kPrimary: engine.SetControl(static_cast<Control>(a.a), a.value); break;
        case Action::kSecondary:
            engine.SetControl(static_cast<Control>(kPrimaryCount + a.a), a.value);
            break;
        case Action::kNoteOn: engine.NoteOn(a.a, a.b, static_cast<unsigned>(a.value)); break;
        case Action::kNoteOff: engine.NoteOff(a.a, a.b); break;
        case Action::kAllOff: engine.AllNotesOff(); break;
        case Action::kBend: engine.SetPitchBend(a.a); break;
        case Action::kFreeze: engine.SetFreeze(a.a != 0); break;
        case Action::kReset: engine.TriggerReset(); break;
        case Action::kSwap: engine.SwapFile(swap_to); break;
    }
}

void ApplyToFirmware(FirmwareLooper& firmware, const Action& a, const FileImage* swap_to) {
    switch(a.kind) {
        case Action::kPrimary: firmware.SetPrimary(a.a, a.value); break;
        case Action::kSecondary: firmware.SetSecondary(a.a, a.value); break;
        case Action::kNoteOn:
            firmware.Midi(0x90, static_cast<std::uint8_t>(a.a), static_cast<std::uint8_t>(a.b),
                          static_cast<std::uint8_t>(a.value));
            break;
        case Action::kNoteOff:
            firmware.Midi(0x80, static_cast<std::uint8_t>(a.a), static_cast<std::uint8_t>(a.b), 0);
            break;
        case Action::kAllOff:
            for(std::uint8_t c = 0; c < 16; ++c)
                firmware.Midi(0xb0, c, 123, 0);
            break;
        case Action::kBend: // a controller that sends its wheel on every channel
            for(std::uint8_t c = 0; c < 16; ++c)
                firmware.Midi(0xe0, c, static_cast<std::uint8_t>(a.a & 0x7f),
                              static_cast<std::uint8_t>(a.a >> 7));
            break;
        case Action::kFreeze: firmware.Freeze(a.a != 0); break;
        case Action::kReset: firmware.ResetPlayhead(); break;
        case Action::kSwap: firmware.ChangeFile(swap_to); break;
    }
}

std::vector<float> RenderPlugin(LooperEngine& engine, const Score& score, std::int64_t quanta,
                                const FileData* swap_to) {
    std::vector<float> out(static_cast<std::size_t>(quanta) * kQ * 2);
    static const float silence[kQ] = {};
    std::size_t next = 0;
    for(std::int64_t q = 0; q < quanta; ++q) {
        for(; next < score.size() && score[next].quantum == q; ++next)
            ApplyToPlugin(engine, score[next], swap_to);
        float* l = out.data() + q * kQ;
        float* r = out.data() + (quanta + q) * kQ;
        engine.Render(silence, silence, l, r);
    }
    return out;
}

std::vector<float> RenderFirmware(FirmwareLooper& firmware, const Score& score, std::int64_t quanta,
                                  const FileImage* swap_to) {
    std::vector<float> out(static_cast<std::size_t>(quanta) * kQ * 2);
    std::size_t next = 0;
    for(std::int64_t q = 0; q < quanta; ++q) {
        for(; next < score.size() && score[next].quantum == q; ++next)
            ApplyToFirmware(firmware, score[next], swap_to);
        firmware.Render(out.data() + q * kQ, out.data() + (quanta + q) * kQ, kQ);
    }
    return out;
}

constexpr float kPrimaryStart[8] = {0.1f, 0.5f, 0.9f, 0.45f, 0.5f, 0.6f, 0.5f, 0.3f};
constexpr float kSecondaryStart[8] = {0.05f, 0, 0.3f, 0.05f, 0.1f, 0.2f, 0, 0.1f};

EngineSettings StartSettings() {
    EngineSettings settings;
    for(unsigned i = 0; i < 8; ++i) {
        settings.control[i] = kPrimaryStart[i];
        settings.control[8 + i] = kSecondaryStart[i];
    }
    // The inputs' secondaries stay 0 in the plug-in; the firmware gets 0 for them
    // too, see kSecondaryStart.
    return settings;
}

class FlushDenormals {
  public:
    FlushDenormals() : saved_(_mm_getcsr()) { _mm_setcsr(saved_ | 0x8040u); }
    ~FlushDenormals() { _mm_setcsr(saved_); }

  private:
    unsigned int saved_;
};

#ifdef NDEBUG
constexpr std::int64_t kSeconds = 12;
#else
constexpr std::int64_t kSeconds = 2;
#endif

void TestAgainstFirmware() {
    // The plug-in's adapter renders with denormals flushed; both sides do it here.
    const FlushDenormals flush;
    const FileImage images[] = {
        MakeImage(4 * 48000 + 77, 48000, 2, 1), MakeImage(3 * 44100 + 5, 44100, 1, 2),
        MakeImage(3 * 44100 + 311, 44100, 2, 3), MakeImage(2 * 48000 + 1000, 48000, 1, 4)};
    const char* names[] = {"stereo 48 kHz", "mono 44.1 kHz", "stereo 44.1 kHz", "mono 48 kHz"};
    const std::int64_t quanta = kSeconds * 48000 / kQ;
    std::size_t capacity = 0;
    for(const FileImage& image : images)
        capacity = std::max(capacity, image.pcm.size());
    FirmwareLooper firmware(capacity);
    LooperEngine engine;
    double total_energy = 0.0;
    for(int f = 0; f < 5; ++f) {
        // Four files, then the first with a change to the second in the middle.
        const int first = f < 4 ? f : 0;
        const std::int64_t swap_at = f < 4 ? -1 : quanta / 2;
        const auto file = ToFile(images[first]);
        const auto swap_file = ToFile(images[1]);
        const Score score = MakeScore(quanta, 100 + static_cast<std::uint32_t>(f), swap_at);
        engine.Reset(file.get(), StartSettings());
        while(engine.TakeReleased()) {}
        firmware.Reset(&images[first], kPrimaryStart, kSecondaryStart);
        const std::vector<float> ours = RenderPlugin(engine, score, quanta, swap_file.get());
        const std::vector<float> theirs = RenderFirmware(firmware, score, quanta, &images[1]);
        std::size_t differing = 0;
        for(std::size_t i = 0; i < ours.size(); ++i)
            differing += std::memcmp(&ours[i], &theirs[i], sizeof(float)) != 0;
        const double energy = Energy(ours);
        total_energy += energy;
        std::printf("  %-34s %lld s, %zu actions: %zu of %zu samples differ, rms %.4f\n",
                    f < 4 ? names[f] : "stereo 48 kHz, change to mono 44.1",
                    static_cast<long long>(kSeconds), score.size(), differing, ours.size(),
                    Rms(ours));
        CHECK(differing == 0);
        CHECK(energy > 0.0);
        if(f == 4) {
            CHECK(engine.CurrentFile() == swap_file.get());
            // The released first file comes back to the owner exactly once.
            CHECK(engine.TakeReleased() == file.get());
            CHECK(engine.TakeReleased() == nullptr);
            engine.Reset(nullptr, EngineSettings{});
            CHECK(engine.TakeReleased() == swap_file.get());
        } else {
            engine.Reset(nullptr, EngineSettings{});
            CHECK(engine.TakeReleased() == file.get());
        }
    }
    CHECK(total_energy > 1.0);
    std::printf("firmware: bit-identical to the separately compiled core (F42 only) in all five "
                "scenarios\n");
}

void TestSettingsOfTheCore() {
    LooperEngine engine;
    CHECK(engine.Schedule() == 0u); // kScheduleCommon
    // The firmware's own default would be the compensated stagger: the setting is real.
    CHECK(FirmwareLooper::ScheduleOfReset() == 2u);
    std::printf("core settings: vocoder on the common grid (firmware default would be %u); "
                "engine state %zu bytes\n",
                FirmwareLooper::ScheduleOfReset(), LooperEngine::StateBytes());
}

// One grain on each channel of a constant file, rectangular window, Blend fully
// granular: after the core's 256-sample fade-in the output is the sample times
// GRAIN_AMP, which must be 0.90 (F42).
void TestGrainLevel() {
    FileData file;
    file.frames = 48000;
    file.rate = 48000;
    file.channels = 2;
    file.pcm.reset(new std::int16_t[96000]);
    std::fill(file.pcm.get(), file.pcm.get() + 96000, std::int16_t{3277}); // about 0.1
    EngineSettings settings;
    settings.control[static_cast<unsigned>(Control::Density)] = 0.0f; // 0.125 grains/s
    settings.control[static_cast<unsigned>(Control::Overlap)] = 1.0f; // 8 s grains
    settings.control[static_cast<unsigned>(Control::Blend)] = 1.0f;   // granular only
    settings.control[static_cast<unsigned>(Control::Window)] = 1.0f;  // rectangle
    LooperEngine engine;
    engine.Reset(&file, settings);
    static const float silence[kQ] = {};
    float left[kQ], right[kQ];
    for(int q = 0; q < 8; ++q)
        engine.Render(silence, silence, left, right);
    const float sample = 3277.0f / 32768.0f;
    for(int i = 0; i < kQ; ++i) {
        CHECK(left[i] == sample * neb::GRAIN_AMP && right[i] == left[i]);
        CHECK(std::fabs(left[i] / sample - 0.90f) < 1e-6f);
    }
    static_assert(neb::GRAIN_AMP == 0.90f, "F42: the grain level of the original");
    engine.Reset(nullptr, EngineSettings{});
    std::printf("grain level: one grain plays at %.4f of its sample (F42: 0.90, firmware 0.2.2: 0.45)\n",
                static_cast<double>(left[0] / sample));
}

void TestTwoInstances() {
    const FlushDenormals flush;
    const FileImage a = MakeImage(2 * 48000, 48000, 2, 11), b = MakeImage(48000, 44100, 1, 12);
    const auto file_a = ToFile(a), file_b = ToFile(b);
    const std::int64_t quanta = 3 * 48000 / kQ;
    const Score score_a = MakeScore(quanta, 21), score_b = MakeScore(quanta, 22);
    LooperEngine alone;
    alone.Reset(file_a.get(), StartSettings());
    const std::vector<float> expected = RenderPlugin(alone, score_a, quanta, nullptr);
    alone.Reset(nullptr, EngineSettings{});

    // Interleaved in one thread.
    LooperEngine first, second;
    first.Reset(file_a.get(), StartSettings());
    second.Reset(file_b.get(), EngineSettings{});
    std::vector<float> interleaved(expected.size());
    static const float silence[kQ] = {};
    std::size_t na = 0, nb = 0;
    float bl[kQ], br[kQ];
    for(std::int64_t q = 0; q < quanta; ++q) {
        for(; na < score_a.size() && score_a[na].quantum == q; ++na)
            ApplyToPlugin(first, score_a[na], nullptr);
        for(; nb < score_b.size() && score_b[nb].quantum == q; ++nb)
            ApplyToPlugin(second, score_b[nb], nullptr);
        second.Render(silence, silence, bl, br);
        first.Render(silence, silence, interleaved.data() + q * kQ,
                     interleaved.data() + (quanta + q) * kQ);
    }
    CHECK(std::memcmp(interleaved.data(), expected.data(), expected.size() * sizeof(float)) == 0);

    // In two threads at once.
    std::vector<float> threaded;
    LooperEngine third, fourth;
    third.Reset(file_a.get(), StartSettings());
    fourth.Reset(file_b.get(), EngineSettings{});
    std::thread other([&] {
        const FlushDenormals inner;
        for(int round = 0; round < 3; ++round)
            RenderPlugin(fourth, score_b, quanta, nullptr);
    });
    threaded = RenderPlugin(third, score_a, quanta, nullptr);
    other.join();
    CHECK(threaded == expected);
    for(LooperEngine* e : {&first, &second, &third, &fourth})
        e->Reset(nullptr, EngineSettings{});
    std::printf("two instances: identical output whatever the other does, interleaved or in "
                "another thread\n");
}

// A swap while the loop plays: the old file fades out, the new one fades in; no
// sample of the swap jumps further than the steady playing of either file does,
// nothing is louder than before, and the audio side allocates nothing.
void TestSwap() {
    const FlushDenormals flush;
    const FileImage a = MakeImage(2 * 48000, 48000, 2, 31), b = MakeImage(2 * 44100, 44100, 1, 32);
    const auto file_a = ToFile(a), file_b = ToFile(b);
    EngineSettings settings = StartSettings();
    LooperEngine engine;
    engine.Reset(file_a.get(), settings);
    static const float silence[kQ] = {};
    std::vector<float> left, right;
    std::uint64_t allocations = 0; // by the engine alone
    auto render = [&](int quanta) {
        float l[kQ], r[kQ];
        for(int q = 0; q < quanta; ++q) {
            {
                const AllocationProbe probe;
                engine.Render(silence, silence, l, r);
                allocations += probe.Count();
            }
            left.insert(left.end(), l, l + kQ);
            right.insert(right.end(), r, r + kQ);
        }
    };
    render(750); // 1 s of the first file
    const std::size_t swap_at = left.size();
    {
        const AllocationProbe probe;
        engine.SwapFile(file_b.get());
        allocations += probe.Count();
    }
    CHECK(engine.Swapping());
    render(20);
    CHECK(!engine.Swapping() && engine.CurrentFile() == file_b.get());
    render(730);
    CHECK(allocations == 0);
    CHECK(engine.TakeReleased() == file_a.get() && engine.TakeReleased() == nullptr);

    auto max_step = [&](std::size_t from, std::size_t to) {
        float worst = 0.0f;
        for(std::size_t i = from + 1; i < to; ++i)
            worst = std::max({worst, std::fabs(left[i] - left[i - 1]), std::fabs(right[i] - right[i - 1])});
        return worst;
    };
    const float steady = std::max(max_step(swap_at - 24000, swap_at), max_step(swap_at + 48000, left.size()));
    const float during = max_step(swap_at, swap_at + 1280);
    float before = 0.0f;
    for(std::size_t i = swap_at - 4800; i < swap_at; ++i)
        before = std::max({before, std::fabs(left[i]), std::fabs(right[i])});
    float louder = 0.0f, quietest = 1e9f;
    for(std::size_t i = swap_at; i < swap_at + 256; ++i)
        louder = std::max({louder, std::fabs(left[i]), std::fabs(right[i])});
    // Somewhere in the swap the output is (nearly) silent: muted, then faded in.
    for(std::size_t i = swap_at; i < swap_at + 1280; i += kQ) {
        double sum = 0.0;
        for(std::size_t k = i; k < i + kQ; ++k)
            sum += left[k] * left[k] + right[k] * right[k];
        quietest = std::min(quietest, static_cast<float>(std::sqrt(sum / (2 * kQ))));
    }
    std::printf("swap: largest step %.4f during the swap, %.4f in steady play; peak %.3f after the "
                "request (%.3f before); quietest quantum rms %.2g; no allocation\n",
                during, steady, louder, before, quietest);
    CHECK(during <= steady);
    CHECK(louder <= before);
    CHECK(quietest < 1e-3f);
    engine.Reset(nullptr, EngineSettings{});
    CHECK(engine.TakeReleased() == file_b.get());

    // A swap requested while one runs: the waiting file is released at once, the
    // newest one comes in.
    engine.Reset(file_a.get(), settings);
    auto file_c = ToFile(a);
    engine.SwapFile(file_b.get());
    engine.SwapFile(file_c.get());
    CHECK(engine.TakeReleased() == file_b.get());
    render(20);
    CHECK(engine.CurrentFile() == file_c.get() && engine.TakeReleased() == file_a.get());
    // Swapping to no file at all: silence afterwards.
    engine.SwapFile(nullptr);
    left.clear();
    right.clear();
    render(40);
    CHECK(engine.CurrentFile() == nullptr && engine.TakeReleased() == file_c.get());
    CHECK(Peak(left, left.size() - kQ) == 0.0f && Peak(right, right.size() - kQ) == 0.0f);
    std::printf("swap: a second request replaces the waiting file; no file means silence\n");
}

void TestVolume() {
    const FlushDenormals flush;
    const FileImage a = MakeImage(48000, 48000, 2, 41);
    const auto file = ToFile(a);
    const std::int64_t quanta = 750;
    Score score = MakeScore(quanta, 42);
    LooperEngine full, varied;
    full.Reset(file.get(), StartSettings());
    EngineSettings half = StartSettings();
    half.control[static_cast<unsigned>(Control::Volume)] = 0.5f;
    varied.Reset(file.get(), half);
    CHECK(full.Volume() == 1.0f && varied.Volume() == 0.5f);
    const std::vector<float> one = RenderPlugin(full, score, quanta, nullptr);
    const std::vector<float> other = RenderPlugin(varied, score, quanta, nullptr);
    for(std::size_t i = 0; i < one.size(); ++i)
        CHECK(other[i] == one[i] * 0.5f);
    // A jump of the target is a ramp of at most kVolumeStep per sample.
    varied.SetControl(Control::Volume, 1.0f);
    float l[kQ], r[kQ];
    static const float silence[kQ] = {};
    varied.Render(silence, silence, l, r);
    CHECK(std::fabs(varied.Volume() - (0.5f + kQ * LooperEngine::kVolumeStep)) < 1e-5f);
    for(int q = 0; q < 4; ++q)
        varied.Render(silence, silence, l, r);
    CHECK(varied.Volume() == 1.0f);
    full.Reset(nullptr, EngineSettings{});
    varied.Reset(nullptr, EngineSettings{});
    std::printf("volume: behind the core, exact scaling, 100 %% leaves the core's samples "
                "unchanged, steps glide in 5 ms\n");
}

std::vector<float> RenderWithDisplay(LooperEngine& engine, const Score& score, std::int64_t quanta,
                                     std::uint64_t& checked, std::uint64_t& allocations) {
    std::vector<float> out(static_cast<std::size_t>(quanta) * kQ * 2);
    static const float silence[kQ] = {};
    std::size_t next = 0;
    for(std::int64_t q = 0; q < quanta; ++q) {
        for(; next < score.size() && score[next].quantum == q; ++next)
            ApplyToPlugin(engine, score[next], nullptr);
        engine.Render(silence, silence, out.data() + q * kQ, out.data() + (quanta + q) * kQ);
        EngineDisplay d;
        {
            const AllocationProbe probe;
            engine.Display(d);
            allocations += probe.Count();
        }
        const double frames = d.frames;
        CHECK(d.frames == engine.CurrentFile()->frames && d.sequence == engine.CurrentFile()->sequence);
        CHECK(d.loop_start >= 0 && d.loop_start < frames && d.loop_length >= 1
              && d.loop_start + d.loop_length <= frames + 1);
        CHECK(d.playhead >= d.loop_start && d.playhead < d.loop_start + d.loop_length);
        CHECK(d.spray >= 0 && d.spray <= frames && d.grain_count <= EngineDisplay::kMaxGrains);
        CHECK(d.frozen == (engine.Frozen() ? 1 : 0) && d.swapping == 0);
        for(std::uint32_t i = 0; i < d.grain_count; ++i) {
            const GrainDot& g = d.grains[i];
            CHECK(g.position >= 0 && g.position <= frames && g.pan >= 0 && g.pan <= 1
                  && g.phase >= 0 && g.phase <= 1);
        }
        ++checked;
    }
    return out;
}

void TestDisplay() {
    const FlushDenormals flush;
    const FileImage a = MakeImage(3 * 48000, 48000, 2, 51), b = MakeImage(48000, 44100, 1, 52);
    auto file_a = ToFile(a), file_b = ToFile(b);
    file_a->sequence = 7;
    file_b->sequence = 8;

    // Reading the display after every quantum leaves the sound bit for bit as it is,
    // with every control moving, notes, Freeze and Reset (the display only reads).
    const std::int64_t quanta = 1500;
    const Score score = MakeScore(quanta, 53);
    LooperEngine plain, shown;
    plain.Reset(file_a.get(), StartSettings());
    shown.Reset(file_a.get(), StartSettings());
    std::uint64_t checked = 0, allocations = 0;
    const std::vector<float> without = RenderPlugin(plain, score, quanta, nullptr);
    const std::vector<float> with = RenderWithDisplay(shown, score, quanta, checked, allocations);
    CHECK(without == with);
    CHECK(plain.Position() == shown.Position() && plain.Resets() == shown.Resets());
    CHECK(allocations == 0);

    // The loop as the core applies START and SIZE, the playhead where the core is.
    EngineSettings settings = StartSettings();
    settings.control[static_cast<unsigned>(Control::Start)] = 0.25f;
    settings.control[static_cast<unsigned>(Control::Size)] = 0.6f;
    settings.control[static_cast<unsigned>(Control::Speed)] = 0.5f;
    settings.control[static_cast<unsigned>(Control::PanRandom)] = 0.0f;
    settings.control[static_cast<unsigned>(Control::Displacement)] = 0.0f;
    settings.control[static_cast<unsigned>(Control::Spray)] = 0.5f;
    LooperEngine engine;
    engine.Reset(file_a.get(), settings);
    static const float silence[kQ] = {};
    float l[kQ], r[kQ];
    std::uint32_t most_grains = 0;
    bool pans_hard = true;
    EngineDisplay d;
    for(int q = 0; q < 750; ++q) {
        engine.Render(silence, silence, l, r);
        engine.Display(d);
        most_grains = std::max(most_grains, d.grain_count);
        for(std::uint32_t i = 0; i < d.grain_count; ++i)
            pans_hard = pans_hard && (d.grains[i].pan == 0.0f || d.grains[i].pan == 1.0f);
    }
    const double frames = a.frames;
    const double start = std::min(double(0.25f) * frames, frames - 1);
    const double length = std::max(1.0, double(0.6f * 0.6f) * (frames - start));
    CHECK(d.loop_start == start && d.loop_length == length);
    double position = std::fmod(engine.Position(), length);
    CHECK(d.playhead == start + position);
    CHECK(std::fabs(d.spray - 0.25 * frames) < 1e-3 * frames); // the secondary, smoothed
    // Grains sound, and without PAN RND the core puts them hard left and right.
    CHECK(most_grains > 0 && pans_hard);

    // Through a swap: the old file while it fades out, then the new one.
    engine.SwapFile(file_b.get());
    engine.Display(d);
    CHECK(d.sequence == 7 && d.swapping == 1);
    bool saw_new = false;
    for(int q = 0; q < 20; ++q) {
        engine.Render(silence, silence, l, r);
        engine.Display(d);
        if(d.sequence == 8) {
            saw_new = true;
            CHECK(d.frames == b.frames && d.swapping == 0 && d.playhead < b.frames);
        } else {
            CHECK(!saw_new && d.sequence == 7 && d.frames == a.frames);
        }
    }
    CHECK(saw_new);
    // What one read costs the audio thread (printed, not checked: timing on a shared
    // machine). With many grains: DENSITY high.
    engine.SetControl(Control::Density, 0.9f);
    for(int q = 0; q < 200; ++q)
        engine.Render(silence, silence, l, r);
    const auto t0 = std::chrono::steady_clock::now();
    std::uint32_t sink = 0;
    constexpr int kReads = 100000;
    for(int i = 0; i < kReads; ++i) {
        engine.Display(d);
        sink += d.grain_count;
    }
    const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / kReads;
    std::printf("display: one read %.0f ns with %u grains, %zu bytes (%u summed)\n", ns, d.grain_count, sizeof(EngineDisplay), sink % 7);
    // No file: nothing to show.
    engine.Reset(nullptr, settings);
    engine.Render(silence, silence, l, r);
    engine.Display(d);
    CHECK(d.frames == 0 && d.sequence == 0 && d.grain_count == 0);
    std::printf("display: read after each of %llu quanta, the sound bit-identical, no allocation; "
                "loop and playhead as the core applies them; up to %u grains, hard left and right "
                "without PAN RND; the old file until the swap, then the new one\n",
                static_cast<unsigned long long>(checked), most_grains);
}

void TestLaws() {
    int checked = 0;
    for(int i = 0; i <= 10000; ++i) {
        const float p = static_cast<float>(i) / 10000.0f;
        CHECK(law::KnobSpeed(p) == neb::KnobSpeed(p));
        CHECK(law::KnobPitch(p) == neb::KnobPitch(p));
        CHECK(law::Speed(p) == neb::LawSpeed(p, false));
        CHECK(law::Pitch(p) == neb::LawPitch(p));
        CHECK(law::Density(p) == neb::LawDensity(p));
        // The inverses land on a pot value that shows the same.
        const double speed = SpeedFactor(p), pitch = PitchOctaves(p), rate = DensityRate(p);
        CHECK(std::fabs(SpeedFactor(SpeedNormalized(speed)) - speed) < 2e-4);
        CHECK(std::fabs(PitchOctaves(PitchNormalized(pitch)) - pitch) < 2e-4);
        CHECK(std::fabs(DensityRate(DensityNormalized(rate)) / rate - 1.0) < 1e-4);
        checked += 8;
    }
    CHECK(SpeedFactor(0.5) == 1.0 && SpeedFactor(0.0) == -4.0 && SpeedFactor(1.0) == 4.0);
    CHECK(PitchOctaves(0.5) == 0.0 && std::fabs(PitchOctaves(0.0) + 3.0) < 1e-6
          && std::fabs(PitchOctaves(1.0) - 2.0) < 1e-6);
    std::printf("laws: display functions equal the core's for %d values; inverses round-trip; "
                "density %.4f to %.2f grains/s\n",
                checked, DensityRate(0.0), DensityRate(1.0));
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    TestSettingsOfTheCore();
    TestGrainLevel();
    TestLaws();
    TestVolume();
    TestSwap();
    TestDisplay();
    TestTwoInstances();
    TestAgainstFirmware();
    std::printf("PASS messier.engine\n");
    return 0;
}
