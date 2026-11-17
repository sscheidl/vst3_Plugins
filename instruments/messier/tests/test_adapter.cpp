// Tests of the streaming adapter (layer 3) with stand-in engines: no sample lost
// or doubled at any block size, events on the 64-sample grid by absolute time at
// every rate, the reported latency against the measured one, no drift over a long
// run, the sidechain input of the engine silent, the note queue's overflow rule,
// and no allocation. The adapter is Kekule's (see stream_adapter.h); these checks
// follow Kekule's adapter test in shorter form.
#define TEST_SUPPORT_COUNT_ALLOCATIONS
#include "test_support.h"

#include "stream_adapter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace messier;
using namespace test_support;

namespace {

constexpr int kQ = StreamAdapter::kQuantum;
constexpr int kBlockSizes[] = {1, 7, 64, 256, 333, 1024};
constexpr double kRates[] = {44100.0, 48000.0, 88200.0, 96000.0, 192000.0};
constexpr int kVaryingBlocks[] = {64, 1, 333, 12, 7, 2048, 16, 48, 5, 1024, 13, 256, 3, 11, 480};

struct FixedBlocks {
    int size;
    int operator()() const { return size; }
};
struct VaryingBlocks {
    std::size_t at = 0;
    int operator()() { return kVaryingBlocks[at++ % std::size(kVaryingBlocks)]; }
};

template <typename NextBlock, typename BeforeBlock>
void Run(StreamAdapter& adapter, std::int64_t total, NextBlock next_block, BeforeBlock before_block,
         std::vector<float>* left_out, std::vector<float>* right_out,
         std::uint64_t* allocations = nullptr) {
    std::vector<float> left(4096), right(4096);
    std::int64_t done = 0;
    while(done < total) {
        const int frames = static_cast<int>(
            std::min<std::int64_t>(std::min<std::int64_t>(next_block(), 4096), total - done));
        before_block(done, frames);
        {
            const AllocationProbe probe;
            adapter.Process(left.data(), right.data(), frames);
            if(allocations)
                *allocations += probe.Count();
        }
        if(left_out)
            left_out->insert(left_out->end(), left.begin(), left.begin() + frames);
        if(right_out)
            right_out->insert(right_out->end(), right.begin(), right.begin() + frames);
        done += frames;
    }
}
const auto kNothing = [](std::int64_t, int) {};

// Left: the index of each internal sample times 2^-25 (exact in float); right:
// its negative. Logs the quantum of every event and checks its input is silent.
constexpr float kCounterScale = 1.0f / 33554432.0f;
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
    void Render(const float* in_left, const float* in_right, float* left, float* right) override {
        for(int i = 0; i < kQ; ++i, ++index) {
            CHECK(in_left[i] == 0.0f && in_right[i] == 0.0f);
            left[i] = static_cast<float>(index) * kCounterScale;
            right[i] = -static_cast<float>(index) * kCounterScale;
        }
    }
    std::int64_t index = 0;
    std::array<Logged, 4096> log{};
    std::size_t logged = 0;
};

// A sine at a fixed internal frequency; a note event adds one impulse.
constexpr double kToneLevel = 0.4;
constexpr float kImpulse = 0.25f;
class ToneEngine final : public QuantumEngine {
  public:
    ToneEngine(double hz, double amplitude) : hz_(hz), amplitude_(amplitude) {}
    void HandleEvent(const EngineEvent& event) override {
        if(event.type == EngineEvent::kNoteOn)
            impulse_ = true;
    }
    void Render(const float*, const float*, float* left, float* right) override {
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
    bool impulse_ = false;
    std::int64_t index_ = 0;
};

EngineEvent Note(unsigned note) {
    EngineEvent event;
    event.type = EngineEvent::kNoteOn;
    event.index = static_cast<std::uint8_t>(note);
    event.data = 100;
    return event;
}

void TestPassThrough() {
    for(const int block : kBlockSizes) {
        CounterEngine engine;
        StreamAdapter adapter(engine);
        CHECK(adapter.Prepare(48000.0));
        CHECK(!adapter.Resampling() && adapter.LatencySamples() == 0);
        std::vector<float> left, right;
        Run(adapter, 48000, FixedBlocks{block}, kNothing, &left, &right);
        for(std::size_t i = 0; i < left.size(); ++i)
            CHECK(left[i] == static_cast<float>(i) * kCounterScale && right[i] == -left[i]);
        CHECK(adapter.RenderedSamples() == (48000 + kQ - 1) / kQ * kQ);
    }
    std::printf("pass-through at 48 kHz: every internal sample once, in order, at block sizes 1 "
                "to 1024; the engine's input silent\n");
}

void TestEventTiming() {
    for(const double rate : kRates) {
        for(int pattern = 0; pattern < 2; ++pattern) {
            CounterEngine engine;
            StreamAdapter adapter(engine);
            CHECK(adapter.Prepare(rate));
            // An event at every 97th host sample; where must it land?
            std::vector<std::int64_t> expected;
            VaryingBlocks varying;
            auto next = [&]() { return pattern ? varying() : 333; };
            Run(adapter, static_cast<std::int64_t>(rate), next,
                [&](std::int64_t done, int frames) {
                    for(std::int64_t t = (done + 96) / 97 * 97; t < done + frames; t += 97) {
                        CHECK(adapter.AddEvent(static_cast<std::int32_t>(t - done), Note(60)));
                        // The first quantum that starts at or after t * 48000 / rate.
                        const double internal = static_cast<double>(t) * 48000.0 / rate;
                        expected.push_back(static_cast<std::int64_t>(std::ceil(internal / kQ - 1e-12)));
                    }
                },
                nullptr, nullptr);
            // The engine renders only as far as the output needs: run on until the
            // last events are due.
            Run(adapter, 4096, FixedBlocks{512}, kNothing, nullptr, nullptr);
            CHECK(engine.logged == expected.size());
            for(std::size_t i = 0; i < std::min(engine.logged, engine.log.size()); ++i)
                CHECK(engine.log[i].quantum == expected[i]);
        }
    }
    std::printf("event timing: on the first 64-sample quantum not before its time, at five rates, "
                "independent of block sizes\n");
}

void TestLatency() {
    std::printf("  latency in host samples:");
    for(const double rate : kRates) {
        ToneEngine engine(1000.0, 0.0);
        StreamAdapter adapter(engine);
        CHECK(adapter.Prepare(rate));
        const std::int64_t impulse_internal = 4800; // a quantum boundary, whole at all rates
        static_assert(4800 % kQ == 0);
        const auto impulse_host = static_cast<std::int64_t>(impulse_internal * rate / 48000.0);
        CHECK(adapter.AddEvent(static_cast<std::int32_t>(impulse_host), Note(60)));
        std::vector<float> left;
        Run(adapter, impulse_host + 4096, VaryingBlocks{}, kNothing, &left, nullptr);
        std::size_t peak_at = 0;
        for(std::size_t i = 0; i < left.size(); ++i)
            if(std::fabs(left[i]) > std::fabs(left[peak_at]))
                peak_at = i;
        const std::int64_t latency = adapter.LatencySamples();
        CHECK(static_cast<std::int64_t>(peak_at) == impulse_host + latency);
        if(!adapter.Resampling())
            CHECK(latency == 0 && left[peak_at] == kImpulse);
        std::printf(" %g Hz: %lld (%.3f ms)", rate, static_cast<long long>(latency),
                    1000.0 * static_cast<double>(latency) / rate);
    }
    std::printf("\nlatency: an impulse appears exactly LatencySamples() after its host time\n");
}

void TestLongRun() {
#ifdef NDEBUG
    const double seconds = 120.0;
#else
    const double seconds = 8.0;
#endif
    for(const double rate : kRates) {
        const double hz = 997.0;
        ToneEngine engine(hz, kToneLevel);
        StreamAdapter adapter(engine);
        CHECK(adapter.Prepare(rate));
        const auto total = static_cast<std::int64_t>(seconds * rate);
        const std::size_t tail = 16384;
        std::vector<float> all;
        std::uint64_t allocations = 0;
        Run(adapter, total - static_cast<std::int64_t>(tail), VaryingBlocks{}, kNothing, nullptr,
            nullptr, &allocations);
        Run(adapter, static_cast<std::int64_t>(tail), VaryingBlocks{}, kNothing, &all, nullptr,
            &allocations);
        CHECK(allocations == 0);
        CHECK(adapter.HostPosition() == static_cast<std::uint64_t>(total));
        const RateRatio& ratio = adapter.Ratio();
        const std::int64_t last = total - 1 - adapter.LatencySamples();
        const std::int64_t last_position = static_cast<std::int64_t>(
            (static_cast<unsigned long long>(last) * ratio.num) / ratio.den);
        const std::int64_t needed =
            last_position + (adapter.Resampling() ? adapter.Kernel().Right() : 0) + 1;
        CHECK(adapter.RenderedSamples() == (needed + kQ - 1) / kQ * kQ);
        CHECK(engine.Index() == adapter.RenderedSamples());
        double worst = 0.0;
        const std::int64_t first = total - static_cast<std::int64_t>(tail);
        for(std::size_t i = 0; i < tail; ++i) {
            const double n = static_cast<double>(first + static_cast<std::int64_t>(i)
                                                 - adapter.LatencySamples());
            worst = std::max(worst, std::fabs(all[i] - kToneLevel * std::sin(2.0 * kPi * hz * n / rate)));
        }
        const double one_sample = 2.0 * kPi * hz / rate;
        std::printf("  %g Hz: %.0f s, %lld host samples, %lld internal rendered, largest deviation "
                    "%.2g (one slipped sample: %.2g)\n",
                    rate, seconds, static_cast<long long>(total),
                    static_cast<long long>(adapter.RenderedSamples()), worst, one_sample);
        CHECK(worst < 1e-4 && worst < one_sample / 100.0);
    }
    std::printf("long run: sample counts exact, no drift against the host clock, no allocation\n");
}

void TestQueues() {
    CounterEngine engine;
    StreamAdapter adapter(engine);
    CHECK(adapter.Prepare(48000.0));
    std::uint64_t allocations = 0;
    {
        const AllocationProbe probe;
        for(int i = 0; i < StreamAdapter::kNoteCapacity; ++i)
            CHECK(adapter.AddEvent(100000, Note(60)));
        CHECK(!adapter.AddEvent(100000, Note(61))); // full: dropped
        EngineEvent off;
        off.type = EngineEvent::kNoteOff;
        off.index = 60;
        CHECK(!adapter.AddEvent(100000, off)); // full: becomes "all notes off"
        EngineEvent control;
        control.type = EngineEvent::kControl;
        control.index = 3;
        for(int i = 0; i < StreamAdapter::kControlCapacity + 5; ++i) {
            control.value = static_cast<float>(i);
            CHECK(adapter.AddEvent(i * kQ, control));
        }
        control.index = StreamAdapter::kControlKinds;
        CHECK(!adapter.AddEvent(0, control));
        allocations += probe.Count();
    }
    CHECK(adapter.DroppedNotes() == 2 && adapter.DroppedControlPoints() == 5);
    Run(adapter, 110000, FixedBlocks{512}, kNothing, nullptr, nullptr, &allocations);
    CHECK(allocations == 0);
    CHECK(adapter.PendingEvents() == 0);
    // The last note event the engine saw is the release of everything.
    std::size_t notes = 0;
    EngineEvent::Type last = EngineEvent::kNoteOn;
    float last_value = -1.0f;
    for(std::size_t i = 0; i < engine.logged; ++i) {
        const EngineEvent& e = engine.log[i].event;
        if(e.type == EngineEvent::kControl)
            last_value = e.value;
        else {
            ++notes;
            last = e.type;
        }
    }
    CHECK(notes == static_cast<std::size_t>(StreamAdapter::kNoteCapacity));
    CHECK(last == EngineEvent::kAllNotesOff);
    CHECK(last_value == static_cast<float>(StreamAdapter::kControlCapacity + 4)); // the latest arrives
    std::printf("queues: a full note queue drops note-ons and turns a note-off into all notes off; "
                "a full control queue keeps the latest value; no allocation\n");
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    TestPassThrough();
    TestEventTiming();
    TestLatency();
    TestLongRun();
    TestQueues();
    std::printf("PASS messier.adapter\n");
    return 0;
}
