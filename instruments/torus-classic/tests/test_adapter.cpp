// Tests of the streaming adapter (layer 3): no sample lost or doubled at any
// block size, events on the quantum grid by absolute time, the resampling
// filter's passband, stopband, delay and long-run position, queue limits,
// reset and freedom from allocation. Most use small stand-in engines; the
// block-size test at the end runs the real dry engine through the adapter.

#define TEST_SUPPORT_COUNT_ALLOCATIONS
#include "test_support.h"

#include "dry_engine.h"
#include "engine_bridge.h"
#include "stream_adapter.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace torus_classic;
using namespace test_support;

namespace {

constexpr int kQ = StreamAdapter::kQuantum;
constexpr int kBlockSizes[] = {1, 7, 12, 16, 48, 64, 333, 1024, 2048};
constexpr double kRates[] = {44100.0, 48000.0, 88200.0, 96000.0, 192000.0};

// Block lengths that change from call to call, repeating.
constexpr int kVaryingBlocks[] = {64, 1, 333, 12, 7, 2048, 16, 48, 5, 1024, 13, 256, 3, 11, 480};

// Runs `total` frames through the adapter in blocks given by `next_block`.
template <typename NextBlock, typename BeforeBlock>
void Run(StreamAdapter& adapter, std::int64_t total, NextBlock next_block,
         BeforeBlock before_block, std::vector<float>* left_out, std::vector<float>* right_out) {
    std::vector<float> left(4096), right(4096);
    std::int64_t done = 0;
    while(done < total) {
        const int frames = static_cast<int>(std::min<std::int64_t>(
            std::min<std::int64_t>(next_block(), 4096), total - done));
        before_block(done, frames);
        adapter.Process(left.data(), right.data(), frames);
        if(left_out)
            left_out->insert(left_out->end(), left.begin(), left.begin() + frames);
        if(right_out)
            right_out->insert(right_out->end(), right.begin(), right.begin() + frames);
        done += frames;
    }
}

struct FixedBlocks {
    int size;
    int operator()() const { return size; }
};
struct VaryingBlocks {
    std::size_t at = 0;
    int operator()() { return kVaryingBlocks[at++ % std::size(kVaryingBlocks)]; }
};
const auto kNothing = [](std::int64_t, int) {};

// --- Stand-in engines --------------------------------------------------------

// Left channel: the index of each internal sample, scaled by 2^-25 so that it
// stays an exact float far below the output limiter's ceiling; right: its
// negative. Remembers in which quantum every event arrived.
constexpr float kCounterScale = 1.0f / 33554432.0f; // 2^-25

class CounterEngine final : public QuantumEngine {
  public:
    struct Logged {
        std::int64_t quantum;
        EngineEvent event;
    };
    void HandleEvent(const EngineEvent& event) override {
        if(logged < log.size())
            log[logged] = {index / kQ, event};
        ++logged;
    }
    void Render(float* left, float* right) override {
        for(int i = 0; i < kQ; ++i, ++index) {
            left[i] = static_cast<float>(index) * kCounterScale;
            right[i] = -static_cast<float>(index) * kCounterScale;
        }
    }
    std::int64_t index = 0;
    std::array<Logged, 4096> log{};
    std::size_t logged = 0;
};

// A sine on the left, a cosine on the right, at a fixed frequency of the
// internal clock, at kToneLevel: below the output limiter's ceiling, so the
// measurements see the rate conversion alone. A note event adds one impulse
// of kImpulse to the next sample.
constexpr double kToneLevel = 0.4;
constexpr float kImpulse = 0.25f;

class ToneEngine final : public QuantumEngine {
  public:
    explicit ToneEngine(double hz, double amplitude = kToneLevel)
    : hz_(hz), amplitude_(amplitude) {}
    void HandleEvent(const EngineEvent& event) override {
        if(event.type == EngineEvent::kNoteOn)
            impulse_ = true;
        else if(event.type == EngineEvent::kControl)
            amplitude_ = event.value;
    }
    void Render(float* left, float* right) override {
        for(int i = 0; i < kQ; ++i, ++index_) {
            const double phase = 2.0 * kPi * hz_ * static_cast<double>(index_) / 48000.0;
            left[i] = static_cast<float>(amplitude_ * std::sin(phase));
            right[i] = static_cast<float>(amplitude_ * std::cos(phase));
            if(impulse_) {
                left[i] += kImpulse;
                impulse_ = false;
            }
        }
    }
    std::int64_t Index() const { return index_; }

  private:
    double hz_, amplitude_;
    std::int64_t index_ = 0;
    bool impulse_ = false;
};

// --- 48 kHz: a FIFO, nothing else -------------------------------------------

void TestPassThrough() {
    for(int variant = 0; variant <= static_cast<int>(std::size(kBlockSizes)); ++variant) {
        CounterEngine engine;
        StreamAdapter adapter(engine);
        CHECK(adapter.Prepare(48000.0));
        CHECK(!adapter.Resampling() && adapter.LatencySamples() == 0);
        const std::int64_t total = 300000;
        std::vector<float> left, right;
        if(variant < static_cast<int>(std::size(kBlockSizes)))
            Run(adapter, total, FixedBlocks{kBlockSizes[variant]}, kNothing, &left, &right);
        else
            Run(adapter, total, VaryingBlocks{}, kNothing, &left, &right);
        // Every sample once, in order.
        for(std::int64_t i = 0; i < total; ++i)
            CHECK(left[i] == static_cast<float>(i) * kCounterScale
                  && right[i] == -static_cast<float>(i) * kCounterScale);
        CHECK(adapter.HostPosition() == static_cast<std::uint64_t>(total));
        // The engine never runs more than one quantum ahead.
        CHECK(adapter.RenderedSamples() == (total + kQ - 1) / kQ * kQ);
    }
    std::printf("48 kHz: every engine sample once and in order for blocks of 1, 7, 12, 16, 48, "
                "64, 333, 1024, 2048 and changing lengths; no delay\n");
}

// --- Events on the quantum grid ---------------------------------------------

// The quantum an event at host sample `host` belongs to: the first one that
// starts at or after the event's internal time.
std::int64_t ExpectedQuantum(std::int64_t host, const RateRatio& ratio) {
    const std::int64_t num = static_cast<std::int64_t>(ratio.num);
    const std::int64_t den = static_cast<std::int64_t>(ratio.den);
    const std::int64_t first_sample = (host * num + den - 1) / den; // ceil
    return (first_sample + kQ - 1) / kQ;
}

void TestEventTiming() {
    for(const double rate : kRates) {
        // Notes at host samples around quantum and block boundaries.
        std::vector<std::int64_t> times;
        for(std::int64_t t : {0, 1, 11, 12, 13, 23, 24, 25, 63, 64, 65, 332, 333, 334, 1023, 1024,
                              1025, 2047, 2048, 2049, 4999, 5000, 5000, 5000, 5001, 7777})
            times.push_back(t);
        for(std::int64_t t = 9000; t < 9100; ++t) // one per host sample
            times.push_back(t);
        std::vector<std::vector<std::int64_t>> fired_by_variant;
        for(int variant = 0; variant <= static_cast<int>(std::size(kBlockSizes)); ++variant) {
            CounterEngine engine;
            StreamAdapter adapter(engine);
            CHECK(adapter.Prepare(rate));
            std::size_t next = 0;
            const auto add = [&](std::int64_t start, int frames) {
                for(; next < times.size() && times[next] < start + frames; ++next)
                    CHECK(adapter.AddEvent(static_cast<std::int32_t>(times[next] - start),
                                           NoteEvent(static_cast<unsigned>(next % 128), 100)));
            };
            const std::int64_t total = 12000;
            if(variant < static_cast<int>(std::size(kBlockSizes)))
                Run(adapter, total, FixedBlocks{kBlockSizes[variant]}, add, nullptr, nullptr);
            else
                Run(adapter, total, VaryingBlocks{}, add, nullptr, nullptr);
            // Let the engine reach the last events (the output lags by the latency).
            Run(adapter, 2048, FixedBlocks{64}, kNothing, nullptr, nullptr);
            CHECK(engine.logged == times.size() && adapter.PendingEvents() == 0);
            std::vector<std::int64_t> fired;
            for(std::size_t i = 0; i < times.size(); ++i) {
                CHECK(engine.log[i].event.index == i % 128); // in the order they were sent
                const std::int64_t expected = ExpectedQuantum(times[i], adapter.Ratio());
                CHECK(engine.log[i].quantum == expected);
                // Never early, less than one quantum late, in internal samples.
                const double internal_time = static_cast<double>(times[i])
                                             * static_cast<double>(adapter.Ratio().num)
                                             / static_cast<double>(adapter.Ratio().den);
                const double delay = static_cast<double>(expected * kQ) - internal_time;
                CHECK(delay >= 0.0 && delay < kQ);
                fired.push_back(engine.log[i].quantum);
            }
            fired_by_variant.push_back(fired);
        }
        for(const auto& fired : fired_by_variant)
            CHECK(fired == fired_by_variant.front()); // the block size does not matter
    }

    // Within one quantum: the later value of a control wins and reaches the
    // engine once; the controls arrive before the notes of that quantum; a
    // note that names an earlier time than the note added before it is played
    // with that note, not before it.
    CounterEngine engine;
    StreamAdapter adapter(engine);
    CHECK(adapter.Prepare(48000.0));
    CHECK(adapter.AddEvent(1, ControlEvent(Control::Damping, 0.1f)));
    CHECK(adapter.AddEvent(5, ControlEvent(Control::Damping, 0.2f)));
    CHECK(adapter.AddEvent(5, ControlEvent(Control::Position, 0.7f)));
    CHECK(adapter.AddEvent(13, ControlEvent(Control::Damping, 0.3f)));
    CHECK(adapter.AddEvent(2, NoteEvent(60, 100)));
    CHECK(adapter.AddEvent(-5, NoteEvent(61, 100))); // a negative offset counts as 0
    CHECK(adapter.PendingEvents() == 5);
    EngineEvent unknown = ControlEvent(Control::Tune, 0.5f);
    unknown.index = StreamAdapter::kControlKinds; // no queue for this control
    CHECK(!adapter.AddEvent(0, unknown) && adapter.PendingEvents() == 5);
    float left[64], right[64];
    adapter.Process(left, right, 64);
    CHECK(engine.logged == 5);
    CHECK(engine.log[0].quantum == 1 && engine.log[0].event.type == EngineEvent::kControl
          && engine.log[0].event.value == 0.2f);
    CHECK(engine.log[1].quantum == 1 && engine.log[1].event.value == 0.7f);
    CHECK(engine.log[2].quantum == 1 && engine.log[2].event.index == 60);
    CHECK(engine.log[3].quantum == 1 && engine.log[3].event.index == 61);
    CHECK(engine.log[4].quantum == 2 && engine.log[4].event.value == 0.3f);
    std::printf("event timing: each event on the first quantum at or after its time, never "
                "early, under 12 internal samples late, the same for every block size, at "
                "44.1, 48, 88.2, 96 and 192 kHz\n");
}

// --- The resampling filter ---------------------------------------------------

struct ToneFit {
    double amplitude, phase; // of amplitude * sin(2 pi f n / rate + phase)
    double residual_rms;     // what is left after removing that sine
};

// Least-squares fit of a sine of known frequency to x[from..to).
ToneFit FitTone(const std::vector<float>& x, double hz, double rate, std::size_t from,
                std::size_t to) {
    double ss = 0, cc = 0, sc = 0, xs = 0, xc = 0;
    for(std::size_t n = from; n < to; ++n) {
        const double w = 2.0 * kPi * hz * static_cast<double>(n) / rate;
        const double s = std::sin(w), c = std::cos(w);
        ss += s * s;
        cc += c * c;
        sc += s * c;
        xs += x[n] * s;
        xc += x[n] * c;
    }
    const double det = ss * cc - sc * sc;
    const double a = (xs * cc - xc * sc) / det; // sine part
    const double b = (xc * ss - xs * sc) / det; // cosine part
    double residual = 0.0;
    for(std::size_t n = from; n < to; ++n) {
        const double w = 2.0 * kPi * hz * static_cast<double>(n) / rate;
        const double e = x[n] - a * std::sin(w) - b * std::cos(w);
        residual += e * e;
    }
    return {std::sqrt(a * a + b * b), std::atan2(b, a),
            std::sqrt(residual / static_cast<double>(to - from))};
}

// Acceptance limits, fixed before the first measurement.
//
// The filter is a Kaiser-windowed sinc designed for a 100 dB stopband
// (SincDesign). Its passband ripple is of the same order as its stopband
// ripple, 1e-5, far below 0.01 dB. Accepted: passband gain within +/-0.01 dB,
// and everything that is not the wanted tone (images, aliases, table
// interpolation, float rounding) at least 90 dB below a full-scale tone; that
// leaves 10 dB between the design figure and the measured one.
constexpr double kPassbandToleranceDb = 0.01;
constexpr double kRejectionDb = -90.0;

void TestResamplerResponse() {
    std::printf("  rate      taps rows latency  passband to  stopband from  worst passband  "
                "worst spurious  worst stopband leak\n");
    const double rates[] = {44100.0,  88200.0,  96000.0,   192000.0, 22050.0, 32000.0,
                            176400.0, 384000.0, 12345.678, 44056.0,  47999.0,
                            1234.5678, 123456.78, 1234567.8};
    for(const double rate : rates) {
        ToneEngine probe(1000.0);
        StreamAdapter design(probe);
        CHECK(design.Prepare(rate));
        const SincKernel& kernel = design.Kernel();
        const double passband = kernel.PassbandEdgeHz(), stopband = kernel.StopbandEdgeHz();
        const double host_nyquist = rate / 2.0;
        CHECK(stopband <= std::min(host_nyquist, 24000.0) + 1e-6);

        std::vector<double> tones;
        for(double hz = 37.0; hz < 23990.0; hz *= 1.09)
            tones.push_back(hz);
        for(double hz : {passband * 0.5, passband * 0.9, passband * 0.99, passband,
                         stopband * 1.0005, stopband * 1.01, 23000.0, 23900.0, 23990.0})
            if(hz < 23995.0)
                tones.push_back(hz);

        double worst_passband_db = 0.0, worst_spurious_db = -400.0, worst_leak_db = -400.0;
        double worst_phase = 0.0;
        for(const double hz : tones) {
            ToneEngine engine(hz);
            StreamAdapter adapter(engine);
            CHECK(adapter.Prepare(rate));
            const int latency = adapter.LatencySamples();
            // Enough output for 60 periods of the lowest tone, at least 8192.
            const std::size_t length =
                std::max<std::size_t>(8192, static_cast<std::size_t>(60.0 * rate / hz));
            const std::size_t skip = static_cast<std::size_t>(2 * latency + 64);
            std::vector<float> left;
            Run(adapter, static_cast<std::int64_t>(skip + length), VaryingBlocks{}, kNothing,
                &left, nullptr);
            for(const float v : left)
                CHECK(std::isfinite(v));
            if(hz >= stopband) {
                // Above the stopband edge nothing may come through, neither
                // the tone (if the host rate could carry it) nor its alias.
                const double leak = Rms(left, skip, skip + length) * std::sqrt(2.0) / kToneLevel;
                worst_leak_db = std::max(worst_leak_db, Decibel(leak));
                continue;
            }
            const ToneFit fit = FitTone(left, hz, rate, skip, skip + length);
            worst_spurious_db =
                std::max(worst_spurious_db,
                         Decibel(fit.residual_rms * std::sqrt(2.0) / kToneLevel));
            if(hz <= passband) {
                const double gain_db = Decibel(fit.amplitude / kToneLevel);
                if(std::fabs(gain_db) > std::fabs(worst_passband_db))
                    worst_passband_db = gain_db;
                // The tone is where the reported latency says it is: the
                // output equals sin(2 pi f (n - latency) / rate).
                double phase = fit.phase + 2.0 * kPi * hz * latency / rate;
                phase = std::remainder(phase, 2.0 * kPi);
                worst_phase = std::max(worst_phase, std::fabs(phase));
            }
        }
        std::printf("  %-9.9g %4d %4d %5d    %8.1f Hz    %8.1f Hz      %+.5f dB     %7.1f dB  "
                    "        %7.1f dB   (phase error %.2g rad, %s rows, table %zu bytes)\n",
                    rate, kernel.Taps(), kernel.Rows(), design.LatencySamples(), passband,
                    stopband, worst_passband_db, worst_spurious_db, worst_leak_db, worst_phase,
                    kernel.Exact() ? "exact" : "interpolated", kernel.TableBytes());
        CHECK(std::fabs(worst_passband_db) <= kPassbandToleranceDb);
        CHECK(worst_spurious_db <= kRejectionDb);
        CHECK(worst_leak_db <= kRejectionDb);
        // A phase error of 1e-3 rad at 20 kHz would be 8 ns, far below a sample.
        CHECK(worst_phase < 1e-3);
    }
    std::printf("resampling filter: passband flat within %.2f dB, images, aliases and stopband "
                "leakage below %.0f dB, tones in phase with the reported latency\n",
                kPassbandToleranceDb, kRejectionDb);
}

void TestLatency() {
    std::printf("  latency in host samples:");
    for(const double rate : kRates) {
        // An impulse at internal sample K appears, centred, at host sample
        // K * rate / 48000 + latency. K is chosen so that this is whole.
        ToneEngine engine(1000.0, 0.0); // silent but for the impulse
        StreamAdapter adapter(engine);
        CHECK(adapter.Prepare(rate));
        const std::int64_t impulse_internal = 4800; // 0.1 s: whole at all five rates
        const auto impulse_host = static_cast<std::int64_t>(impulse_internal * rate / 48000.0);
        CHECK(adapter.AddEvent(static_cast<std::int32_t>(impulse_host), NoteEvent(60, 100)));
        std::vector<float> left;
        Run(adapter, impulse_host + 4096, VaryingBlocks{}, kNothing, &left, nullptr);
        std::size_t peak_at = 0;
        for(std::size_t i = 0; i < left.size(); ++i)
            if(std::fabs(left[i]) > std::fabs(left[peak_at]))
                peak_at = i;
        const std::int64_t latency = adapter.LatencySamples();
        CHECK(static_cast<std::int64_t>(peak_at) == impulse_host + latency);
        // Symmetric around its peak: a linear-phase filter.
        for(std::size_t k = 1; k < 40; ++k)
            CHECK(std::fabs(left[peak_at - k] - left[peak_at + k]) < 1e-6f);
        // Nothing before the filter can know of the impulse.
        const double reach = adapter.Resampling()
                                 ? (adapter.Kernel().Right() + 1) * rate / 48000.0 + 1.0
                                 : 0.0;
        for(std::size_t i = 0; i + static_cast<std::size_t>(reach) < peak_at; ++i)
            CHECK(left[i] == 0.0f);
        if(!adapter.Resampling())
            CHECK(latency == 0 && left[peak_at] == kImpulse);
        std::printf(" %g Hz: %lld (%.3f ms)", rate, static_cast<long long>(latency),
                    1000.0 * static_cast<double>(latency) / rate);
    }
    std::printf("\nlatency: an impulse appears exactly LatencySamples() after its host time, "
                "symmetric, with silence before the filter's reach\n");
}

void TestLongRun() {
#ifdef NDEBUG
    const double seconds = 180.0;
#else
    const double seconds = 12.0; // unoptimised builds are slow; the arithmetic is the same
#endif
    for(const double rate : kRates) {
        const double hz = 997.0;
        ToneEngine engine(hz);
        StreamAdapter adapter(engine);
        CHECK(adapter.Prepare(rate));
        const auto total = static_cast<std::int64_t>(seconds * rate);
        const std::size_t tail = 16384;
        std::vector<float> all;
        // Keep only the end; the start is covered by the other tests.
        Run(adapter, total - static_cast<std::int64_t>(tail), VaryingBlocks{}, kNothing, nullptr,
            nullptr);
        Run(adapter, static_cast<std::int64_t>(tail), VaryingBlocks{}, kNothing, &all, nullptr);
        CHECK(adapter.HostPosition() == static_cast<std::uint64_t>(total));

        // Sample count: the engine has rendered exactly what the last output
        // sample needs, rounded up to a quantum. No sample more or fewer after
        // any length of time.
        const RateRatio& ratio = adapter.Ratio();
        const std::int64_t last = total - 1 - adapter.LatencySamples();
        const std::int64_t last_position = static_cast<std::int64_t>(
            (static_cast<unsigned long long>(last) * ratio.num) / ratio.den); // floor
        const std::int64_t needed =
            last_position + (adapter.Resampling() ? adapter.Kernel().Right() : 0) + 1;
        CHECK(adapter.RenderedSamples() == (needed + kQ - 1) / kQ * kQ);
        CHECK(engine.Index() == adapter.RenderedSamples());

        // Position: after `seconds` the tone still has the phase the host's
        // clock says, sample for sample. One slipped sample would show as an
        // error of 2 pi * 997 / rate rad or more.
        double worst = 0.0;
        const std::int64_t first = total - static_cast<std::int64_t>(tail);
        for(std::size_t i = 0; i < tail; ++i) {
            const double n = static_cast<double>(first + static_cast<std::int64_t>(i)
                                                 - adapter.LatencySamples());
            const double ideal = kToneLevel * std::sin(2.0 * kPi * hz * n / rate);
            worst = std::max(worst, std::fabs(all[i] - ideal));
        }
        const double one_sample = 2.0 * kPi * hz / rate;
        std::printf("  %g Hz: %.0f s, %lld host samples, %lld internal samples rendered, "
                    "largest deviation from the ideal tone %.2g (one slipped sample: %.2g)\n",
                    rate, seconds, static_cast<long long>(total),
                    static_cast<long long>(adapter.RenderedSamples()), worst, one_sample);
        CHECK(worst < 1e-4 && worst < one_sample / 100.0);
    }
    std::printf("long run: sample counts exact and no drift against the host clock\n");
}

// --- Queue limits and reset --------------------------------------------------

void TestQueueLimits() {
    float left[2048], right[2048];
    // Notes: kNoteCapacity wait; the next one is refused and counted.
    {
        CounterEngine engine;
        StreamAdapter adapter(engine);
        CHECK(adapter.Prepare(48000.0));
        for(int i = 0; i < StreamAdapter::kNoteCapacity; ++i)
            CHECK(adapter.AddEvent(1000, NoteEvent(static_cast<unsigned>(i % 128), 100)));
        CHECK(adapter.PendingEvents() == StreamAdapter::kNoteCapacity);
        CHECK(!adapter.AddEvent(1000, NoteEvent(1, 100)) && adapter.DroppedNotes() == 1);
        // A control sent for the same sample still finds room, in a queue of
        // its own, and waits for its time like everything else: with the
        // notes' queue full, nothing reaches the engine early.
        CHECK(adapter.AddEvent(1000, ControlEvent(Control::Damping, 0.25f)));
        CHECK(engine.logged == 0 && adapter.DroppedControlPoints() == 0);
        CHECK(adapter.PendingEvents() == StreamAdapter::kNoteCapacity + 1);
        adapter.Process(left, right, 996); // up to the quantum before the events'
        CHECK(engine.logged == 0);
        adapter.Process(left, right, 2048 - 996);
        CHECK(adapter.PendingEvents() == 0);
        CHECK(engine.logged == 1u + StreamAdapter::kNoteCapacity);
        CHECK(engine.log[0].event.type == EngineEvent::kControl && engine.log[0].quantum == 84
              && engine.log[0].event.value == 0.25f);
        for(int i = 0; i < StreamAdapter::kNoteCapacity; ++i) // order kept
            CHECK(engine.log[1 + i].event.index == i % 128 && engine.log[1 + i].quantum == 84);
    }

    // One control with more points waiting than its queue holds, each for a
    // quantum of its own; the value of a point names that quantum. The
    // newest waiting point gives way to the next: steps are lost, but no
    // value arrives before its quantum, and the last arrives at its own.
    const int points = StreamAdapter::kControlCapacity + 44;
    std::vector<std::pair<std::int64_t, float>> overloaded, unhurried;
    {
        CounterEngine engine;
        StreamAdapter adapter(engine);
        CHECK(adapter.Prepare(48000.0));
        for(int i = 1; i <= points; ++i)
            CHECK(adapter.AddEvent(12 * i, ControlEvent(Control::Tune, static_cast<float>(i))));
        CHECK(adapter.DroppedControlPoints() == 44);
        CHECK(adapter.PendingEvents() == StreamAdapter::kControlCapacity && engine.logged == 0);
        adapter.Process(left, right, 2048);
        adapter.Process(left, right, 2048);
        CHECK(engine.logged == static_cast<std::size_t>(StreamAdapter::kControlCapacity));
        for(std::size_t k = 0; k < engine.logged; ++k) {
            const auto sent_for = static_cast<std::int64_t>(engine.log[k].event.value);
            CHECK(engine.log[k].quantum == sent_for); // on time: not early, not late
            overloaded.emplace_back(engine.log[k].quantum, engine.log[k].event.value);
        }
        CHECK(overloaded.back().second == static_cast<float>(points)); // the latest value
        CHECK(overloaded[StreamAdapter::kControlCapacity - 2].second
              == static_cast<float>(StreamAdapter::kControlCapacity - 1));
    }
    // The same points sent block by block, where the queue never fills: all
    // arrive. What arrived under overload is a part of that, at the same
    // quanta; the difference is exactly what was counted as dropped.
    {
        CounterEngine engine;
        StreamAdapter adapter(engine);
        CHECK(adapter.Prepare(48000.0));
        int next = 1;
        for(int start = 0; start < 4096; start += 64) {
            for(; next <= points && 12 * next < start + 64; ++next)
                CHECK(adapter.AddEvent(12 * next - start,
                                       ControlEvent(Control::Tune, static_cast<float>(next))));
            adapter.Process(left, right, 64);
        }
        CHECK(adapter.DroppedControlPoints() == 0 && engine.logged == static_cast<std::size_t>(points));
        for(std::size_t k = 0; k < engine.logged; ++k)
            unhurried.emplace_back(engine.log[k].quantum, engine.log[k].event.value);
        std::size_t found = 0;
        for(const auto& arrived : overloaded)
            found += std::find(unhurried.begin(), unhurried.end(), arrived) != unhurried.end();
        CHECK(found == overloaded.size() && unhurried.size() - overloaded.size() == 44);
    }

    // A full queue of one control does not touch another control or the notes.
    {
        CounterEngine engine;
        StreamAdapter adapter(engine);
        CHECK(adapter.Prepare(48000.0));
        for(int i = 1; i <= points; ++i)
            CHECK(adapter.AddEvent(12 * i, ControlEvent(Control::Tune, static_cast<float>(i))));
        CHECK(adapter.AddEvent(24, ControlEvent(Control::Damping, 0.5f)));
        CHECK(adapter.AddEvent(24, ModelEvent(3)) && adapter.AddEvent(24, BendEvent(100)));
        CHECK(adapter.AddEvent(24, NoteEvent(60, 100)));
        adapter.Process(left, right, 36); // quanta 0, 1, 2
        CHECK(engine.logged == 6); // Tune for quanta 1 and 2, and the four at quantum 2
        std::size_t at_two = 0;
        for(std::size_t k = 0; k < engine.logged; ++k)
            at_two += engine.log[k].quantum == 2;
        CHECK(at_two == 5 && engine.log[0].quantum == 1);
    }

    // Parameter flushes without audio: values pile up on one quantum and
    // replace each other; the queues do not grow.
    {
        CounterEngine engine;
        StreamAdapter adapter(engine);
        CHECK(adapter.Prepare(48000.0));
        for(int i = 0; i < 100000; ++i) {
            CHECK(adapter.AddEvent(0, ControlEvent(Control::Tune, static_cast<float>(i % 100) / 100.0f)));
            CHECK(adapter.AddEvent(0, BendEvent(static_cast<unsigned>(i % 16384))));
            CHECK(adapter.AddEvent(0, ModelEvent(static_cast<unsigned>(i % 6))));
        }
        CHECK(adapter.PendingEvents() == 3 && adapter.DroppedControlPoints() == 0);
        adapter.Process(left, right, 1);
        CHECK(engine.logged == 3);
        CHECK(engine.log[0].event.value == 0.99f && engine.log[1].event.index == 99999 % 6
              && engine.log[2].event.data == 99999 % 16384);
    }

    // Points of one control that come out of order: the later one acts with
    // the earlier one, not before it.
    {
        CounterEngine engine;
        StreamAdapter adapter(engine);
        CHECK(adapter.Prepare(48000.0));
        CHECK(adapter.AddEvent(500, ControlEvent(Control::Damping, 0.5f)));
        CHECK(adapter.AddEvent(100, ControlEvent(Control::Damping, 0.6f)));
        adapter.Process(left, right, 1024);
        CHECK(engine.logged == 1 && engine.log[0].quantum == 42 && engine.log[0].event.value == 0.6f);
    }
    std::printf("queue limits: %d notes and %d points per control wait at most; further notes "
                "are refused and counted, a full control queue loses steps but nothing acts "
                "before its time, repeated flushes do not pile up\n",
                StreamAdapter::kNoteCapacity, StreamAdapter::kControlCapacity);
}

void TestResetAndRates() {
    // A reset gives the same output as a fresh adapter, at every rate, also
    // after a change of rate.
    for(const double rate : kRates) {
        const auto render = [&](StreamAdapter& adapter) {
            std::vector<float> left;
            const auto add = [&](std::int64_t start, int) {
                if(start == 0)
                    CHECK(adapter.AddEvent(100, NoteEvent(60, 100)));
            };
            Run(adapter, 6000, FixedBlocks{480}, add, &left, nullptr);
            return left;
        };
        ToneEngine fresh_engine(440.0);
        StreamAdapter fresh(fresh_engine);
        CHECK(fresh.Prepare(rate));
        const auto expected = render(fresh);

        ToneEngine used_engine(440.0);
        StreamAdapter used(used_engine);
        CHECK(used.Prepare(rate == 96000.0 ? 44100.0 : 96000.0)); // another rate first
        Run(used, 5000, VaryingBlocks{}, kNothing, nullptr, nullptr);
        CHECK(used.AddEvent(40000, NoteEvent(60, 100))); // still waiting at the change
        CHECK(used.Prepare(rate));
        CHECK(used.PendingEvents() == 0 && used.HostPosition() == 0);
        used_engine = ToneEngine(440.0);
        CHECK(render(used) == expected);
        // And once more after a plain reset with events waiting.
        CHECK(used.AddEvent(40000, NoteEvent(60, 100)));
        used.Reset();
        CHECK(used.PendingEvents() == 0);
        used_engine = ToneEngine(440.0);
        CHECK(render(used) == expected);
    }
    // Rates the adapter refuses leave it as it was.
    ToneEngine engine(440.0);
    StreamAdapter adapter(engine);
    CHECK(adapter.Prepare(44100.0));
    for(const double bad : {0.0, -48000.0, 999.0, 4.1e6, std::nan(""), HUGE_VAL})
        CHECK(!adapter.Prepare(bad));
    CHECK(adapter.HostRate() == 44100.0);
    // An adapter that was never prepared does nothing and does not crash.
    StreamAdapter idle(engine);
    float left[16] = {}, right[16] = {};
    CHECK(!idle.AddEvent(0, NoteEvent(60, 100)));
    idle.Process(left, right, 16);
    std::printf("reset and rate changes: the same output as a fresh adapter, waiting events "
                "gone; unusable rates refused\n");
}

void TestNoAllocation() {
    for(const double rate : kRates) {
        ToneEngine engine(440.0);
        StreamAdapter adapter(engine);
        CHECK(adapter.Prepare(rate)); // allocates, here
        std::vector<float> left(4096), right(4096);
        const AllocationProbe probe;
        VaryingBlocks blocks;
        for(int i = 0; i < 400; ++i) {
            const int frames = blocks();
            adapter.AddEvent(frames / 2, NoteEvent(60, 100));
            adapter.AddEvent(frames / 3, ControlEvent(Control::Tune, 0.5f));
            adapter.Process(left.data(), right.data(), frames);
            if(i % 100 == 99)
                adapter.Reset();
        }
        CHECK(probe.Count() == 0);
        std::printf("  %g Hz: adapter memory %zu bytes\n", rate, adapter.AllocatedBytes());
    }
    std::printf("allocation: none in AddEvent, Process and Reset (operator new counted)\n");
}

// --- The output stage: low-pass and limiter ----------------------------------

std::vector<float> ThroughStage(OutputStage& stage, const std::vector<float>& in) {
    std::vector<float> out = in; // interleaved stereo
    for(std::size_t i = 0; i + 1 < out.size(); i += 2)
        stage.Process(out[i], out[i + 1]);
    return out;
}

void TestOutputStage() {
    Lcg random(99);
    // Filter open, level below the ceiling: every sample passes as it came.
    {
        OutputStage stage;
        stage.Prepare(48000.0);
        std::vector<float> quiet(96000);
        for(float& v : quiet)
            v = (random.Unit() * 2.0f - 1.0f) * OutputStage::kCeiling;
        CHECK(ThroughStage(stage, quiet) == quiet && stage.Gain() == 1.0f);
    }
    // Limiter: whatever comes in, no sample leaves above the ceiling; once the
    // loud part is over the gain returns to exactly 1 and samples pass as
    // they came again.
    for(const double rate : kRates) {
        OutputStage stage;
        stage.Prepare(rate);
        std::vector<float> loud;
        for(int i = 0; i < 20000; ++i) { // bursts up to 8 times the ceiling, and a square
            const float burst = (i / 500) % 2 ? 4.0f : 0.3f;
            loud.push_back((random.Unit() * 2.0f - 1.0f) * burst);
            loud.push_back(i % 64 < 32 ? 2.0f : -2.0f);
        }
        const auto limited = ThroughStage(stage, loud);
        float peak = 0.0f, peak_in = 0.0f;
        for(std::size_t i = 0; i < limited.size(); ++i) {
            CHECK(std::isfinite(limited[i]));
            peak = std::max(peak, std::fabs(limited[i]));
            peak_in = std::max(peak_in, std::fabs(loud[i]));
        }
        CHECK(peak_in > 3.0f && peak <= OutputStage::kCeiling && peak > 0.4f);
        CHECK(stage.Gain() < 1.0f);
        // Four seconds of stereo: from an eighth of full gain the release
        // (0.15 s time constant) needs about 1.4 s to be back at 1.
        std::vector<float> quiet(static_cast<std::size_t>(8.0 * rate), 0.0f);
        for(float& v : quiet)
            v = (random.Unit() * 2.0f - 1.0f) * 0.2f;
        const auto after = ThroughStage(stage, quiet);
        CHECK(stage.Gain() == 1.0f);
        const std::size_t tail = quiet.size() / 2; // the last two seconds
        CHECK(std::equal(after.begin() + static_cast<std::ptrdiff_t>(tail), after.end(),
                         quiet.begin() + static_cast<std::ptrdiff_t>(tail)));
        // While it recovers the level only rises towards the input's.
        for(std::size_t i = 0; i < tail; ++i)
            CHECK(std::fabs(after[i]) <= std::fabs(quiet[i]));
    }
    // Low-pass: flat below the cutoff, 3 dB down at it, 12 dB per octave above.
    std::printf("  low-pass at 1 kHz:");
    for(const double rate : {44100.0, 48000.0, 96000.0}) {
        const float control = static_cast<float>(
            OutputStage::kTop * std::log(1000.0 / OutputStage::kLowHz)
            / std::log(OutputStage::kHighHz / OutputStage::kLowHz));
        const auto gain_db = [&](double hz) {
            OutputStage stage;
            stage.Prepare(rate);
            stage.JumpFilter(control);
            std::vector<float> tone;
            const int frames = static_cast<int>(rate);
            for(int n = 0; n < frames; ++n) {
                const auto v = static_cast<float>(0.25 * std::sin(2.0 * kPi * hz * n / rate));
                tone.push_back(v);
                tone.push_back(v);
            }
            const auto out = ThroughStage(stage, tone);
            std::vector<float> left;
            for(std::size_t i = 0; i < out.size(); i += 2) {
                CHECK(out[i] == out[i + 1]); // both channels alike
                left.push_back(out[i]);
            }
            return Decibel(ToneAmplitude(left, hz, rate, left.size() / 2, left.size()) / 0.25);
        };
        const double low = gain_db(100.0), at = gain_db(1000.0), two = gain_db(2000.0),
                     four = gain_db(4000.0);
        std::printf(" %g Hz: %.2f dB at 100 Hz, %.2f at 1 kHz, %.2f at 2 kHz, %.2f at 4 kHz;", rate,
                    low, at, two, four);
        CHECK(std::fabs(low) < 0.1 && std::fabs(at + 3.01) < 0.2);
        CHECK(std::fabs(two + 12.3) < 0.8 && std::fabs(four + 24.1) < 1.2);
    }
    std::printf("\n");
    // The filter control: set, it glides and lands exactly; back at 1 the
    // samples pass as they came again.
    {
        OutputStage stage;
        stage.Prepare(48000.0);
        stage.SetFilter(0.2f);
        float l = 0.1f, r = 0.1f;
        stage.Process(l, r);
        CHECK(stage.FilterValue() < 1.0f && stage.FilterValue() > 0.2f);
        for(int i = 0; i < 48000; ++i)
            stage.Process(l, r);
        CHECK(stage.FilterValue() == 0.2f && stage.FilterTarget() == 0.2f);
        stage.SetFilter(std::nanf("")); // not a number: 0
        CHECK(stage.FilterTarget() == 0.0f);
        stage.SetFilter(7.0f);
        CHECK(stage.FilterTarget() == 1.0f);
        std::vector<float> quiet(96000);
        for(float& v : quiet)
            v = (random.Unit() * 2.0f - 1.0f) * 0.3f;
        const auto out = ThroughStage(stage, quiet);
        CHECK(stage.FilterValue() == 1.0f);
        CHECK(std::equal(out.begin() + 48000, out.end(), quiet.begin() + 48000));
    }
    // Through the adapter the filter control is an event like the others: it
    // changes when its quantum is rendered, not before the sample it was sent
    // for, and not later than grid and latency allow.
    for(const double rate : kRates) {
        ToneEngine engine(440.0);
        StreamAdapter adapter(engine);
        CHECK(adapter.Prepare(rate));
        const int at = 1000;
        CHECK(adapter.AddEvent(at, FilterEvent(0.3f)));
        std::vector<float> left(4096), right(4096);
        adapter.Process(left.data(), right.data(), at);
        CHECK(adapter.Output().FilterTarget() == 1.0f); // not before its sample
        const int allowance =
            adapter.LatencySamples() + static_cast<int>(std::ceil(kQ * rate / 48000.0)) + 1;
        adapter.Process(left.data(), right.data(), allowance);
        CHECK(adapter.Output().FilterTarget() == 0.3f);
        adapter.Reset();
        adapter.JumpOutputFilter(0.5f);
        CHECK(adapter.Output().FilterValue() == 0.5f);
    }
    std::printf("output stage: untouched below the ceiling with the filter open; no sample "
                "above %.2f whatever comes in, exact again after the release; low-pass 12 dB "
                "per octave\n",
                OutputStage::kCeiling);
}

// --- The real engine through the adapter ------------------------------------

struct HostEvent {
    std::int64_t sample;
    EngineEvent event;
};

// A performance in host samples: notes on and around block and quantum
// boundaries, dense chords, moving controls, bends and model changes.
std::vector<HostEvent> HostScore(std::int64_t total) {
    std::vector<HostEvent> score;
    Lcg random(4711);
    for(std::int64_t t : {0, 1, 11, 12, 13, 47, 48, 63, 64, 65, 332, 333, 334, 1023, 1024, 2047,
                          2048, 2049})
        score.push_back({t, NoteEvent(48 + static_cast<unsigned>(t % 36), 100)});
    for(unsigned i = 0; i < 9; ++i) // nine notes on one sample: three quanta of triggers
        score.push_back({3000, NoteEvent(40 + 4 * i, 110)});
    std::int64_t t = 3500;
    while(t < total) {
        t += 1 + random.Below(1400);
        if(t >= total)
            break;
        switch(random.Below(10)) {
            case 0:
            case 1:
            case 2:
            case 3: {
                const unsigned count = 1 + random.Below(4);
                for(unsigned i = 0; i < count; ++i)
                    score.push_back({t, NoteEvent(random.Below(128), 1 + random.Below(127))});
                break;
            }
            case 4:
            case 5:
            case 6:
            case 7:
                score.push_back({t, ControlEvent(static_cast<Control>(random.Below(kControlCount)),
                                                 random.Unit())});
                break;
            case 8: score.push_back({t, BendEvent(random.Below(16384))}); break;
            default:
                if(random.Below(4) == 0)
                    score.push_back({t, ModelEvent(random.Below(6))});
                break;
        }
    }
    return score;
}

template <typename NextBlock>
std::vector<float> RenderEngine(double rate, const std::vector<HostEvent>& score,
                                std::int64_t total, NextBlock next_block, unsigned model) {
    DryEngine engine;
    engine.Reset(0x12345678);
    engine.JumpModel(model);
    EngineBridge bridge(engine);
    StreamAdapter adapter(bridge);
    CHECK(adapter.Prepare(rate));
    std::size_t next = 0;
    const auto add = [&](std::int64_t start, int frames) {
        for(; next < score.size() && score[next].sample < start + frames; ++next)
            CHECK(adapter.AddEvent(static_cast<std::int32_t>(score[next].sample - start),
                                   score[next].event));
    };
    std::vector<float> left, right;
    Run(adapter, total, next_block, add, &left, &right);
    left.insert(left.end(), right.begin(), right.end());
    return left;
}

void TestBlockSizesWithEngine() {
    std::size_t rate_index = 0;
    for(const double rate : kRates) {
#ifdef NDEBUG
        const auto total = static_cast<std::int64_t>(2.0 * rate);
#else
        const auto total = static_cast<std::int64_t>(0.5 * rate);
#endif
        const auto score = HostScore(total);
        static constexpr unsigned kStartModels[] = {1, 0, 2, 5, 3};
        const unsigned model = kStartModels[rate_index++];
        const auto reference = RenderEngine(rate, score, total, FixedBlocks{64}, model);
        CHECK(Energy(reference) > 1.0);
        for(const float v : reference)
            CHECK(std::isfinite(v));
        // The limiter at the end of the chain holds every sample, at every rate.
        CHECK(Peak(reference) <= OutputStage::kCeiling);
        for(const int size : kBlockSizes)
            CHECK(RenderEngine(rate, score, total, FixedBlocks{size}, model) == reference);
        CHECK(RenderEngine(rate, score, total, VaryingBlocks{}, model) == reference);
        std::printf("  %g Hz: %zu events, %lld samples, identical for all block sizes (RMS "
                    "%.4f, peak %.4f)\n",
                    rate, score.size(), static_cast<long long>(total), Rms(reference),
                    Peak(reference));
    }

    // At 48 kHz the adapter converts nothing: the output is the engine's own
    // through the output stage (filter open; the limiter acts on the loud
    // parts), with every event applied on the quantum the rule names.
    const std::int64_t total = 48000;
    const auto score = HostScore(total);
    const auto through_adapter = RenderEngine(48000.0, score, total, VaryingBlocks{}, 0);
    DryEngine engine;
    engine.Reset(0x12345678);
    engine.JumpModel(0);
    EngineBridge bridge(engine);
    OutputStage stage;
    stage.Prepare(48000.0);
    std::vector<float> left, right;
    float l[kQ], r[kQ];
    std::size_t next = 0;
    for(std::int64_t quantum = 0; quantum * kQ < total; ++quantum) {
        for(; next < score.size() && (score[next].sample + kQ - 1) / kQ <= quantum; ++next)
            bridge.HandleEvent(score[next].event);
        engine.Render(l, r);
        for(int i = 0; i < kQ; ++i)
            stage.Process(l[i], r[i]);
        left.insert(left.end(), l, l + kQ);
        right.insert(right.end(), r, r + kQ);
    }
    left.resize(static_cast<std::size_t>(total));
    right.resize(static_cast<std::size_t>(total));
    left.insert(left.end(), right.begin(), right.end());
    CHECK(left == through_adapter);
    std::printf("block sizes with the real engine: one absolute event list gives one result "
                "at 44.1, 48, 88.2, 96 and 192 kHz; at 48 kHz it is the engine's own output\n");
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    TestPassThrough();
    TestEventTiming();
    TestResamplerResponse();
    TestLatency();
    TestLongRun();
    TestQueueLimits();
    TestResetAndRates();
    TestNoAllocation();
    TestOutputStage();
    TestBlockSizesWithEngine();
    std::printf("PASS\n");
    return 0;
}
