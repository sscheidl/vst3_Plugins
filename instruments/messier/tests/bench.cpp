// Measurements for the build report; run by hand, not a test. Renders the looper
// engine through the adapter, as the processor does, and reports the share of one
// core and the slowest host block, per setting and host rate. Also counts how often
// the core's soft limit at its output (above 0.9) acts, now that grains play at
// 0.90 (F42). Numbers of one machine and one compiler; no guarantee.
#include "engine_bridge.h"
#include "looper_engine.h"
#include "stream_adapter.h"

#include <intrin.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace messier;

namespace {

std::string CpuName() {
    int info[4] = {};
    char name[49] = {};
    for(int i = 0; i < 3; ++i) {
        __cpuid(info, static_cast<int>(0x80000002u + i));
        std::memcpy(name + 16 * i, info, sizeof(info));
    }
    std::string text(name);
    text.erase(0, text.find_first_not_of(' '));
    return text;
}

std::unique_ptr<FileData> MakeFile(std::uint32_t frames, std::uint32_t rate, std::uint16_t channels,
                                   double level) {
    auto file = std::make_unique<FileData>();
    file->frames = frames;
    file->rate = rate;
    file->channels = channels;
    file->pcm.reset(new std::int16_t[static_cast<std::size_t>(frames) * channels]);
    std::uint32_t seed = 1;
    for(std::uint32_t i = 0; i < frames; ++i)
        for(unsigned c = 0; c < channels; ++c) {
            seed = seed * 1664525u + 1013904223u;
            const double t = static_cast<double>(i) / rate;
            const double x = level * (0.6 * std::sin(6.283185307 * (c ? 331.0 : 220.0) * t)
                                      + 0.4 * ((seed >> 9) / 8388608.0 - 1.0));
            file->pcm[static_cast<std::size_t>(i) * channels + c] =
                static_cast<std::int16_t>(std::lround(std::clamp(x, -1.0, 1.0) * 32767.0));
        }
    return file;
}

struct Setting {
    const char* name;
    float density, overlap, pitch, blend, window, spray, pitch_random, displacement;
};

void Measure(const Setting& s, const FileData& file, double rate, int block, double seconds) {
    LooperEngine engine;
    EngineBridge bridge(engine);
    StreamAdapter adapter(bridge);
    adapter.Prepare(rate);
    EngineSettings settings;
    settings.control[static_cast<unsigned>(Control::Density)] = s.density;
    settings.control[static_cast<unsigned>(Control::Overlap)] = s.overlap;
    settings.control[static_cast<unsigned>(Control::Pitch)] = s.pitch;
    settings.control[static_cast<unsigned>(Control::Blend)] = s.blend;
    settings.control[static_cast<unsigned>(Control::Window)] = s.window;
    settings.control[static_cast<unsigned>(Control::Spray)] = s.spray;
    settings.control[static_cast<unsigned>(Control::PitchRandom)] = s.pitch_random;
    settings.control[static_cast<unsigned>(Control::Displacement)] = s.displacement;
    engine.Reset(&file, settings);
    std::vector<float> left(static_cast<std::size_t>(block)), right(left.size());
    const auto total = static_cast<std::int64_t>(seconds * rate);
    using Clock = std::chrono::steady_clock;
    double busy = 0.0, worst = 0.0;
    std::int64_t limited = 0, samples = 0;
    float peak = 0.0f;
    for(std::int64_t done = 0; done < total; done += block) {
        const auto start = Clock::now();
        adapter.Process(left.data(), right.data(), block);
        const double spent = std::chrono::duration<double>(Clock::now() - start).count();
        busy += spent;
        worst = std::max(worst, spent);
        for(int i = 0; i < block; ++i) {
            const float a = std::max(std::fabs(left[static_cast<std::size_t>(i)]), std::fabs(right[static_cast<std::size_t>(i)]));
            peak = std::max(peak, a);
            limited += a > 0.9f;
        }
        samples += block;
    }
    const double real = static_cast<double>(total) / rate;
    std::printf("| %s | %g kHz | %d | %.2f %% | %.0f us of %.0f us | %.3f | %.3f %% |\n", s.name,
                rate / 1000.0, block, 100.0 * busy / real, 1e6 * worst, 1e6 * block / rate, peak,
                100.0 * static_cast<double>(limited) / static_cast<double>(samples));
}

} // namespace

int main() {
    std::printf("Messier bench: %s, MSVC %d, %s build, one thread\n\n", CpuName().c_str(), _MSC_FULL_VER,
#ifdef NDEBUG
                "Release"
#else
                "Debug"
#endif
    );
    const auto stereo = MakeFile(10 * 48000, 48000, 2, 0.7);
    const auto quiet = MakeFile(10 * 48000, 48000, 2, 0.25);
    const auto mono441 = MakeFile(10 * 44100, 44100, 1, 0.7);
    const Setting settings[] = {
        {"defaults (Blend 0: vocoder only)", 0.4f, 0.5f, 0.5f, 0.0f, 0.0f, 0, 0, 0},
        {"half and half", 0.4f, 0.5f, 0.5f, 0.5f, 0.0f, 0, 0, 0},
        {"Density, Overlap at maximum, Blend 0.5", 1.0f, 1.0f, 0.5f, 0.5f, 0.5f, 0.2f, 0.3f, 0.0f},
        {"the same, Pitch at maximum", 1.0f, 1.0f, 1.0f, 0.5f, 1.0f, 0.2f, 0.3f, 0.0f},
        {"granular only, dense, rectangle", 1.0f, 1.0f, 0.5f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f},
        {"granular only, sparse (F42 range)", 0.3f, 0.3f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    };
    std::printf("| Setting | Host rate | Block | Share of one core | Slowest block | Peak | Samples above 0.9 |\n");
    std::printf("| --- | --- | --- | --- | --- | --- | --- |\n");
    for(const Setting& s : settings)
        for(const double rate : {48000.0, 192000.0})
            for(const int block : {64, 256})
                Measure(s, *stereo, rate, block, 10.0);
    Measure({"dense, mono 44.1 kHz file", 1.0f, 1.0f, 1.0f, 0.5f, 1.0f, 0.2f, 0.3f, 0}, *mono441, 44100.0, 256, 10.0);
    Measure({"granular only, dense, quiet file", 1.0f, 1.0f, 0.5f, 1.0f, 1.0f, 0, 0, 0}, *quiet, 48000.0, 256, 10.0);
    return 0;
}
