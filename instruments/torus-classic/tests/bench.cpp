// Measurements for the build report, not a pass/fail test: what rendering
// costs per model, what a model change costs, what a reset costs, and what
// denormal numbers would cost without the adapter's flush-to-zero. The
// numbers hold for this machine and build only.
//
// Usage: bench_torus_classic [seconds per measurement, default 20]

#include "dry_engine.h"
#include "engine_bridge.h"
#include "stream_adapter.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <xmmintrin.h>

using namespace torus_classic;

namespace {

using Clock = std::chrono::steady_clock;
constexpr int kQ = DryEngine::kQuantum;
const char* const kNames[6] = {"Modal Resonator",    "Sympathetic Strings", "Inharmonic Strings",
                               "FM Voice",           "Western Chords",      "Stereo Strings"};

double Microseconds(Clock::duration d) {
    return std::chrono::duration<double, std::micro>(d).count();
}

struct Flush {
    unsigned int saved = _mm_getcsr();
    explicit Flush(bool on) {
        if(on)
            _mm_setcsr(saved | 0x8040u);
    }
    ~Flush() { _mm_setcsr(saved); }
};

double Percentile(std::vector<double> v, double p) {
    std::sort(v.begin(), v.end());
    return v[static_cast<std::size_t>(p * static_cast<double>(v.size() - 1))];
}

// One 12-sample quantum lasts 250 microseconds of audio.
constexpr double kQuantumBudget = 1e6 * kQ / 48000.0;

void Rendering(double seconds) {
    std::printf("Rendering through the adapter, four voices re-struck twice a second, default "
                "controls, %.0f s of audio per row.\n", seconds);
    std::printf("  model                rate     block   real-time load   per second of audio\n");
    for(const double rate : {48000.0, 44100.0, 96000.0, 192000.0}) {
        for(int model = 0; model < 6; ++model) {
            DryEngine engine;
            engine.Reset();
            engine.JumpModel(static_cast<unsigned>(model));
            EngineBridge bridge(engine);
            StreamAdapter adapter(bridge);
            adapter.Prepare(rate);
            const int block = 64;
            std::vector<float> left(block), right(block);
            const auto total = static_cast<long long>(seconds * rate);
            const auto strike_every = static_cast<long long>(rate / 2);
            const auto start = Clock::now();
            for(long long done = 0; done < total; done += block) {
                if(done % strike_every < block)
                    for(unsigned note : {48u, 55u, 60u, 64u})
                        adapter.AddEvent(0, NoteEvent(note + static_cast<unsigned>(model), 100));
                adapter.Process(left.data(), right.data(), block);
            }
            const double spent = Microseconds(Clock::now() - start);
            std::printf("  %-20s %-8g %-5d   %6.2f %%         %7.1f us\n", kNames[model], rate,
                        block, 100.0 * spent / (seconds * 1e6), spent / seconds);
        }
    }
}

void Quanta(double seconds) {
    std::printf("\nSingle quanta of the engine at 48 kHz (budget %.0f us each), sounding as "
                "above, %.0f s per model.\n", kQuantumBudget, seconds);
    std::printf("  model                median    99.9 %%     longest\n");
    for(int model = 0; model < 6; ++model) {
        const Flush flush(true);
        DryEngine engine;
        engine.Reset();
        engine.JumpModel(static_cast<unsigned>(model));
        const auto quanta = static_cast<int>(seconds * 4000.0);
        std::vector<double> times(static_cast<std::size_t>(quanta));
        float left[kQ], right[kQ];
        for(int q = 0; q < quanta; ++q) {
            if(q % 2000 == 0)
                for(unsigned note : {48u, 55u, 60u, 64u})
                    engine.NoteOn(note + static_cast<unsigned>(model), 100);
            const auto start = Clock::now();
            engine.Render(left, right);
            times[static_cast<std::size_t>(q)] = Microseconds(Clock::now() - start);
        }
        std::printf("  %-20s %5.2f us  %6.2f us  %7.2f us\n", kNames[model],
                    Percentile(times, 0.5), Percentile(times, 0.999), Percentile(times, 1.0));
    }
}

void ModelChanges(int repetitions) {
    std::printf("\nModel changes: the one quantum in which the new model's resonators are set "
                "up, %d changes per pair, four voices sounding. The fade around it does not "
                "shorten this quantum.\n", repetitions);
    std::printf("  to model             median     longest    (longest of the 40 quanta around "
                "it, without the change itself)\n");
    for(int to = 0; to < 6; ++to) {
        std::vector<double> change_times, neighbour_times;
        for(int repetition = 0; repetition < repetitions; ++repetition) {
            const Flush flush(true);
            const int from = (to + 1 + repetition % 5) % 6;
            DryEngine engine;
            engine.Reset();
            engine.JumpModel(static_cast<unsigned>(from));
            float left[kQ], right[kQ];
            for(unsigned note : {48u, 55u, 60u, 64u})
                engine.NoteOn(note, 100);
            for(int q = 0; q < 200; ++q)
                engine.Render(left, right);
            engine.SetModel(static_cast<unsigned>(to));
            double neighbours = 0.0, change = 0.0;
            for(int q = 0; q < 40; ++q) {
                const unsigned before = engine.Model();
                const auto start = Clock::now();
                engine.Render(left, right);
                const double spent = Microseconds(Clock::now() - start);
                if(before != engine.Model())
                    change = spent; // the model was replaced and configured in this quantum
                else
                    neighbours = std::max(neighbours, spent);
            }
            change_times.push_back(change);
            neighbour_times.push_back(neighbours);
        }
        std::printf("  %-20s %6.2f us  %7.2f us   (%.2f us)\n", kNames[to],
                    Percentile(change_times, 0.5), Percentile(change_times, 1.0),
                    Percentile(neighbour_times, 1.0));
    }
}

void Resets(int repetitions) {
    std::vector<double> times;
    DryEngine engine;
    for(int i = 0; i < repetitions; ++i) {
        const auto start = Clock::now();
        engine.Reset();
        times.push_back(Microseconds(Clock::now() - start));
    }
    std::printf("\nEngine reset (as on activation or after loading a state), %d times: median "
                "%.1f us, longest %.1f us; %zu bytes cleared and initialised.\n",
                repetitions, Percentile(times, 0.5), Percentile(times, 1.0),
                DryEngine::StateBytes());
}

void Denormals() {
    std::printf("\nDecay into silence, Modal Resonator, Damping 0: mean time per quantum in "
                "the fourth second after a strike.\n");
    for(const bool flush_on : {false, true}) {
        const Flush flush(flush_on);
        DryEngine engine;
        engine.Reset();
        engine.JumpControl(Control::Damping, 0.0f);
        for(unsigned note : {48u, 55u, 60u, 64u})
            engine.NoteOn(note, 127);
        float left[kQ], right[kQ];
        for(int q = 0; q < 12000; ++q)
            engine.Render(left, right);
        const auto start = Clock::now();
        for(int q = 0; q < 4000; ++q)
            engine.Render(left, right);
        const double each = Microseconds(Clock::now() - start) / 4000.0;
        std::printf("  denormals %-28s %7.2f us per quantum (%.1f %% of real time)\n",
                    flush_on ? "flushed to zero (as shipped):" : "computed (float default):", each,
                    100.0 * each / kQuantumBudget);
    }
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const double seconds = argc > 1 ? std::atof(argv[1]) : 20.0;
#ifdef NDEBUG
    std::printf("Torus Classic benchmark, optimised build.\n\n");
#else
    std::printf("Torus Classic benchmark, UNOPTIMISED build: the numbers say nothing.\n\n");
#endif
    Rendering(seconds);
    Quanta(seconds);
    ModelChanges(300);
    Resets(2000);
    Denormals();
    return 0;
}
