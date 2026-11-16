// Measurements for the build report, not a pass/fail test: what rendering
// costs per model with 4, 8 and 16 voices, what a model change and a change
// of the number of voices cost, what a reset costs, how long a note takes to
// come to rest, and what denormal numbers would cost without the adapter's
// flush-to-zero. The numbers hold for this machine and build only.
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

// As many notes as there are voices, so that every voice sounds: a chord in
// fourths and thirds from C2 up.
void StrikeAll(DryEngine& engine, unsigned voices, unsigned transpose = 0) {
    for(unsigned i = 0; i < voices; ++i)
        engine.NoteOn(36 + transpose + 3 * i + (i % 4), 100);
}

void Rendering(double seconds) {
    std::printf("Rendering through the adapter, every voice re-struck twice a second (4, 8 or "
                "16 notes at once), default controls, %.0f s of audio per row.\n", seconds);
    std::printf("  model                rate     voices  real-time load   per second of audio\n");
    for(const double rate : {48000.0, 44100.0, 96000.0, 192000.0}) {
        for(const unsigned voices : {4u, 8u, 16u}) {
            if(voices == 8 && rate != 48000.0)
                continue;
            for(int model = 0; model < 6; ++model) {
                DryEngine engine;
                engine.Reset();
                engine.JumpModel(static_cast<unsigned>(model));
                engine.JumpPolyphony(voices);
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
                        for(unsigned i = 0; i < voices; ++i)
                            adapter.AddEvent(0, NoteEvent(36 + static_cast<unsigned>(model)
                                                              + 3 * i + (i % 4),
                                                          100));
                    adapter.Process(left.data(), right.data(), block);
                }
                const double spent = Microseconds(Clock::now() - start);
                std::printf("  %-20s %-8g %-6u  %6.2f %%         %7.1f us\n", kNames[model],
                            rate, voices, 100.0 * spent / (seconds * 1e6), spent / seconds);
            }
        }
    }
}

void Quanta(double seconds) {
    std::printf("\nSingle quanta of the engine at 48 kHz (budget %.0f us each), sounding as "
                "above, %.0f s per row.\n", kQuantumBudget, seconds);
    std::printf("  model                voices  median    99.9 %%     longest\n");
    for(const unsigned voices : {4u, 16u}) {
        for(int model = 0; model < 6; ++model) {
            const Flush flush(true);
            DryEngine engine;
            engine.Reset();
            engine.JumpModel(static_cast<unsigned>(model));
            engine.JumpPolyphony(voices);
            const auto quanta = static_cast<int>(seconds * 4000.0);
            std::vector<double> times(static_cast<std::size_t>(quanta));
            float left[kQ], right[kQ];
            for(int q = 0; q < quanta; ++q) {
                if(q % 2000 == 0)
                    StrikeAll(engine, voices, static_cast<unsigned>(model));
                const auto start = Clock::now();
                engine.Render(left, right);
                times[static_cast<std::size_t>(q)] = Microseconds(Clock::now() - start);
            }
            std::printf("  %-20s %-6u  %5.2f us  %6.2f us  %7.2f us\n", kNames[model], voices,
                        Percentile(times, 0.5), Percentile(times, 0.999),
                        Percentile(times, 1.0));
        }
    }
}

void ModelChanges(int repetitions, unsigned voices) {
    std::printf("\nModel changes with %u voices sounding: the one quantum in which the new "
                "model's resonators are set up, %d changes per pair. The fade around it does "
                "not shorten this quantum.\n", voices, repetitions);
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
            engine.JumpPolyphony(voices);
            float left[kQ], right[kQ];
            StrikeAll(engine, voices);
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

void PolyphonyChanges(int repetitions) {
    std::printf("\nFrom sixteen voices to four while all sixteen sound, %d times per model: "
                "the longest of the 40 quanta after the change (three banks fade out and are "
                "put back in one of them), and the longest of the 40 before it.\n",
                repetitions);
    std::printf("  model                longest after   longest before\n");
    for(int model = 0; model < 6; ++model) {
        double after = 0.0, before = 0.0;
        for(int repetition = 0; repetition < repetitions; ++repetition) {
            const Flush flush(true);
            DryEngine engine;
            engine.Reset();
            engine.JumpModel(static_cast<unsigned>(model));
            engine.JumpPolyphony(16);
            float left[kQ], right[kQ];
            StrikeAll(engine, 16);
            for(int q = 0; q < 200; ++q) {
                const auto start = Clock::now();
                engine.Render(left, right);
                if(q >= 160)
                    before = std::max(before, Microseconds(Clock::now() - start));
            }
            engine.SetPolyphony(4);
            for(int q = 0; q < 40; ++q) {
                const auto start = Clock::now();
                engine.Render(left, right);
                after = std::max(after, Microseconds(Clock::now() - start));
            }
        }
        std::printf("  %-20s %7.2f us      %7.2f us\n", kNames[model], after, before);
    }
}

void Idle() {
    // Plug-in Nap is not supported (see the README): an instance that sounds
    // nothing is still rendered. This is what that costs.
    std::printf("\nAn instance that sounds nothing: time per quantum. With more than four "
                "voices, a bank that has been below -180 dBFS for a second is put away and "
                "costs nothing.\n");
    const Flush flush(true);
    float left[kQ], right[kQ];
    {
        DryEngine engine;
        engine.Reset();
        const auto start = Clock::now();
        for(int q = 0; q < 40000; ++q)
            engine.Render(left, right);
        const double each = Microseconds(Clock::now() - start) / 40000.0;
        std::printf("  never struck (Modal Resonator, any number of voices): %.2f us, %.2f %% "
                    "of real time\n",
                    each, 100.0 * each / kQuantumBudget);
    }
    {
        // Sixteen short notes, then silence: how long until three banks are put away.
        DryEngine engine;
        engine.Reset();
        engine.JumpControl(Control::Damping, 0.0f); // the engine's sense: short notes
        engine.JumpPolyphony(16);
        for(unsigned i = 0; i < 16; ++i)
            engine.NoteOn(40 + 2 * i, 127);
        unsigned quantum = 0;
        for(; quantum < 4000u * 120u && (quantum < 10 || engine.RenderedBanks() > 1); ++quantum)
            engine.Render(left, right);
        std::printf("  sixteen voices, sixteen short notes (Modal Resonator, Damping 1): three "
                    "banks put away after %.1f s\n",
                    quantum / 4000.0);
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
        // Without the flush the engine is not at rest by then: what is left
        // of its sound lies above the smallest normal numbers until it is
        // denormal, and the denormal numbers are what costs.
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
    ModelChanges(300, 4);
    ModelChanges(300, 16);
    PolyphonyChanges(100);
    Resets(2000);
    Idle();
    Denormals();
    return 0;
}
