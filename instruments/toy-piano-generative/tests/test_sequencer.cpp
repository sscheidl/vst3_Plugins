// Checks the generative sequencer against its rules: the shares and rates a
// rule states, measured over many trials, and the things that must hold in
// every single step.
//
// The sequencer's generator is seeded with fixed numbers, so every run sees
// the same trials. Each measured share must lie within five standard
// deviations of its expected value; that bound was fixed before the first run.
#include "generative_sequencer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

using namespace taureon::generative;

#define CHECK(condition) \
    do { \
        if(!(condition)) { \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            std::exit(1); \
        } \
    } while(false)

namespace {

constexpr double kSigmas = 5.0;

// The scale lists once more, typed in independently of the sequencer's table.
const std::vector<std::vector<int>> kLists = {
    {0, 2, 4, 5, 7, 9, 11, 0}, {0, 2, 3, 5, 7, 8, 10, 0}, {0, 3, 5, 7, 10, 0},
    {-2, 0, 1, 13, 0, 7, 8, 8}, {0, 12, 0},               {0, 7, 12, 0},
    {0},
};
constexpr int kA = 9; // the default tonic, in semitones

using Steps = std::array<std::int8_t, kNumSteps>;

// A share of yes/no trials against the share the rule states.
void CheckShare(const char* what, long long hits, long long trials, double expected) {
    CHECK(trials > 0);
    const double observed = static_cast<double>(hits) / static_cast<double>(trials);
    const double bound =
        kSigmas * std::sqrt(expected * (1.0 - expected) / static_cast<double>(trials));
    std::printf("  %-52s %.4f, rule %.4f +/- %.4f (n = %lld)\n", what, observed, expected, bound,
                trials);
    CHECK(std::fabs(observed - expected) <= bound);
}

// A count of events whose chances differ from trial to trial. `expected` is
// the sum of the chances; the variance of such a sum is at most that.
void CheckCount(const char* what, long long hits, double expected) {
    const double bound = kSigmas * std::sqrt(expected);
    std::printf("  %-52s %lld, rule %.0f +/- %.0f\n", what, hits, expected, bound);
    CHECK(std::fabs(static_cast<double>(hits) - expected) <= bound);
}

// The chance that a drawn note lies `relative` semitones above the tonic.
double NoteChance(int mode, int relative) {
    const std::vector<int>& list = kLists[static_cast<std::size_t>(mode)];
    const int spreads = mode == 6 ? 1 : 2;
    double chance = 0.0;
    for(int octave = 0; octave < spreads; ++octave) {
        if(12 * octave == relative)
            chance += 0.30 / spreads;
        for(const int entry : list)
            if(12 * octave + entry == relative)
                chance += 0.70 / static_cast<double>(list.size()) / spreads;
    }
    return chance;
}

// The chance that one draw, rest included, gives exactly this step.
double DrawChance(int mode, int probability, int tonic, std::int8_t step) {
    const double p = probability / 100.0;
    return step == kRest ? 1.0 - p : p * NoteChance(mode, step - tonic);
}

int PitchClass(int relative) {
    return ((relative % 12) + 12) % 12;
}

int Differences(const Steps& a, const Steps& b) {
    int count = 0;
    for(int i = 0; i < kNumSteps; ++i)
        count += a[static_cast<std::size_t>(i)] != b[static_cast<std::size_t>(i)];
    return count;
}

// The start-up pattern: every step the tonic or a rest.
void TestStartPattern() {
    long long steps = 0, notes = 0;
    for(std::uint64_t seed = 1; seed <= 20000; ++seed) {
        const Sequencer sequencer(seed);
        const State& state = sequencer.state();
        CHECK(state.tonic == kDefaultTonic && state.scale_mode == 1 && state.algorithm == 1
              && state.mutation == 0 && state.note_probability == 65 && state.octave == 2
              && state.length == 16 && kClockDivides[state.clock_divide] == 1);
        for(const std::int8_t step : state.steps) {
            CHECK(step == kRest || step == kA); // never a low stray note
            ++steps;
            notes += step != kRest;
        }
    }
    std::printf("start-up pattern:\n");
    CheckShare("steps holding the tonic", notes, steps, 0.65);
}

// Drawing: rests, the tonic's share and the two octaves.
void TestDraws() {
    std::printf("draws, minor, note probability 65 %%:\n");
    Sequencer sequencer(1);
    long long draws = 0, rests = 0, tonic = 0, upper = 0;
    for(int i = 0; i < 20000; ++i) {
        sequencer.ChooseScaleMode(1);
        for(const std::int8_t step : sequencer.state().steps) {
            ++draws;
            if(step == kRest) {
                ++rests;
                continue;
            }
            CHECK(NoteChance(1, step - kA) > 0.0); // a note the rule can give
            tonic += PitchClass(step - kA) == 0;
            upper += step - kA >= 12;
        }
    }
    CheckShare("rests", rests, draws, 0.35);
    CheckShare("notes on the tonic's pitch class", tonic, draws - rests, 0.475);
    CheckShare("notes in the upper octave", upper, draws - rests, 0.50);

    std::printf("the tonic's share of the notes, by scale mode:\n");
    const double expected[kNumScaleModes] = {0.475, 0.475, 0.30 + 0.70 * 2 / 6, 0.475, 1.0,
                                             0.825, 1.0};
    sequencer.SetNoteProbability(100);
    for(int mode = 0; mode < kNumScaleModes; ++mode) {
        long long notes = 0, on_tonic = 0, eights = 0, raised = 0;
        for(int i = 0; i < 5000; ++i) {
            sequencer.ChooseScaleMode(mode);
            for(const std::int8_t step : sequencer.state().steps) {
                CHECK(step != kRest && NoteChance(mode, step - kA) > 0.0);
                ++notes;
                on_tonic += PitchClass(step - kA) == 0;
                eights += step - kA == 8 || step - kA == 20;
                raised += step != kA;
            }
        }
        char name[32];
        std::snprintf(name, sizeof(name), "mode %d", mode);
        CheckShare(name, on_tonic, notes, expected[mode]);
        if(mode == 3) // its list holds one entry twice
            CheckShare("mode 3, the doubled entry", eights, notes, 0.70 * 2 / 8);
        if(mode == kTonicOnlyMode)
            CHECK(raised == 0); // no octave spread: the tonic itself, always
    }
}

// Choosing a mode redraws; a pattern without a note gets one.
void TestRedraw() {
    Sequencer sequencer(9);
    sequencer.SetNoteProbability(1);
    int rescued = 0;
    const int redraws = 6000;
    for(int i = 0; i < redraws; ++i) {
        const int mode = i % kNumScaleModes;
        sequencer.ChooseScaleMode(mode);
        const Steps& steps = sequencer.state().steps;
        int notes = 0;
        for(const std::int8_t step : steps)
            notes += step != kRest;
        CHECK(notes >= 1);
        const bool lifted = mode == kTonicOnlyMode ? steps[0] == kA + 12
                                                   : steps[0] == kA + 12 || steps[0] == kA + 24;
        rescued += notes == 1 && lifted;
    }
    // With 1 % notes, 85 % of all redraws come out empty and need the rescue.
    CHECK(rescued > redraws * 80 / 100);
    sequencer.SetNoteProbability(0);
    for(int mode = 0; mode < kNumScaleModes; ++mode) {
        sequencer.ChooseScaleMode(mode);
        for(const std::int8_t step : sequencer.state().steps)
            CHECK(step == kRest); // notes switched off: nothing is rescued
    }
    // Choosing an algorithm leaves the pattern alone.
    sequencer.SetNoteProbability(65);
    sequencer.ChooseScaleMode(0);
    const Steps before = sequencer.state().steps;
    for(int algorithm = 1; algorithm <= kNumAlgorithms; ++algorithm) {
        sequencer.SetAlgorithm(algorithm);
        CHECK(sequencer.state().steps == before);
    }
    std::printf("redraw: an empty pattern gets one raised tonic; none at 0 %%; algorithms keep "
                "the pattern\n");
}

// A new tonic transposes the stored notes; rests stay rests.
void TestTonic() {
    Sequencer sequencer(5);
    sequencer.ChooseScaleMode(1);
    const Steps original = sequencer.state().steps;
    const auto shifted_by = [&](int semitones) {
        const Steps& steps = sequencer.state().steps;
        for(std::size_t i = 0; i < steps.size(); ++i)
            if(original[i] == kRest ? steps[i] != kRest : steps[i] != original[i] + semitones)
                return false;
        return true;
    };
    sequencer.SetTonic(0); // A to C
    CHECK(shifted_by(-9));
    sequencer.SetTonic(11); // C to B
    CHECK(shifted_by(2));
    sequencer.SetTonic(1); // a black key: C sharp
    CHECK(shifted_by(-8));
    sequencer.SetTonic(kDefaultTonic);
    CHECK(sequencer.state().steps == original);
    std::printf("tonic: stored notes transposed, rests kept\n");
}

// Algorithm 1: one of all sixteen steps is redrawn with the mutation chance.
void TestAlgorithm1() {
    std::printf("algorithm 1, mutation 40 %%, length 4:\n");
    Sequencer sequencer(7);
    sequencer.SetMutation(40);
    sequencer.SetLength(4);
    const int ticks = 200000;
    double expected = 0.0, differing = 0.0;
    long long changed = 0;
    std::array<long long, kNumSteps> per_slot{};
    for(int tick = 0; tick < ticks; ++tick) {
        const Steps before = sequencer.state().steps;
        for(const std::int8_t step : before)
            differing += 1.0 - DrawChance(1, 65, kA, step);
        sequencer.Tick();
        const Steps& after = sequencer.state().steps;
        CHECK(Differences(before, after) <= 1);
        for(std::size_t i = 0; i < after.size(); ++i)
            if(after[i] != before[i]) {
                ++changed;
                ++per_slot[i];
            }
    }
    // A redraw shows only if the new step differs from the old one.
    expected = 0.40 / kNumSteps * differing;
    CheckCount("steps changed", changed, expected);
    std::printf("  that is a redraw chance per step and tick of %.5f; rule 40 %% / 16 = 0.02500\n",
                0.40 / kNumSteps * static_cast<double>(changed) / expected);
    // Every step takes its share, also the twelve beyond the length.
    for(const long long count : per_slot)
        CHECK(std::fabs(static_cast<double>(count) - expected / kNumSteps)
              <= kSigmas * std::sqrt(expected / kNumSteps));
    std::printf("  all 16 steps change equally often, also those beyond the length\n");
}

// Algorithm 2: on top of algorithm 1, a run of three notes.
void TestAlgorithm2() {
    std::printf("algorithm 2:\n");
    {
        // In the tonic-only mode a run is the tonic and twice its octave, and
        // nothing else can write the octave.
        Sequencer sequencer(11);
        sequencer.SetAlgorithm(2);
        sequencer.SetMutation(50);
        sequencer.SetNoteProbability(60);
        sequencer.ChooseScaleMode(kTonicOnlyMode);
        const int ticks = 100000;
        long long runs = 0;
        for(int tick = 0; tick < ticks; ++tick) {
            State blank = sequencer.state();
            blank.steps.fill(kRest);
            CHECK(sequencer.Load(blank));
            sequencer.Tick();
            const Steps& steps = sequencer.state().steps;
            bool run = false;
            for(std::size_t i = 0; i < steps.size(); ++i)
                run = run
                      || (steps[i] == kA && steps[(i + 1) % kNumSteps] == kA + 12
                          && steps[(i + 2) % kNumSteps] == kA + 12);
            int octaves = 0;
            for(const std::int8_t step : steps)
                octaves += step == kA + 12;
            CHECK(octaves == (run ? 2 : 0));
            runs += run;
        }
        CheckShare("ticks with a run, mutation 50 %, notes 60 %", runs, ticks, 0.50 * 0.60);
    }
    // The shape of a run in every mode: three steps in a row, up the list by
    // one or two entries, an octave higher past its end.
    for(int mode = 0; mode < kNumScaleModes; ++mode) {
        const std::vector<int>& list = kLists[static_cast<std::size_t>(mode)];
        const int size = static_cast<int>(list.size());
        Sequencer sequencer(21 + static_cast<std::uint64_t>(mode));
        sequencer.SetAlgorithm(2);
        sequencer.SetMutation(100);
        sequencer.SetNoteProbability(100);
        sequencer.ChooseScaleMode(mode);
        for(int tick = 0; tick < 2000; ++tick) {
            State blank = sequencer.state();
            blank.steps.fill(kRest);
            CHECK(sequencer.Load(blank));
            sequencer.Tick();
            const Steps& steps = sequencer.state().steps;
            int notes = 0;
            for(const std::int8_t step : steps)
                notes += step != kRest;
            CHECK(notes == 3 || notes == 4); // the run, and the one redrawn step beside it
            bool found = false;
            for(int slot = 0; slot < kNumSteps && !found; ++slot)
                for(int start = 0; start < size && !found; ++start)
                    for(int stride = 1; stride <= 2 && !found; ++stride)
                        for(int octave = 0; octave < (mode == 6 ? 1 : 2) && !found; ++octave) {
                            bool match = true;
                            for(int i = 0; i < 3; ++i) {
                                const int index = start + i * stride;
                                const int note =
                                    kA + 12 * octave + list[static_cast<std::size_t>(index % size)]
                                    + (index >= size ? 12 : 0);
                                match = match
                                        && steps[static_cast<std::size_t>((slot + i) % kNumSteps)]
                                               == note;
                            }
                            found = match;
                        }
            CHECK(found);
        }
    }
    std::printf("  every run: three steps in a row, up the list, an octave higher past its end\n");
}

// Algorithm 3 plays the scale list itself.
void TestAlgorithm3() {
    for(int mode = 0; mode < kNumScaleModes; ++mode)
        for(const int length : {16, 5, 1}) {
            Sequencer a(1), b(2);
            a.SetNoteProbability(100);
            a.ChooseScaleMode(mode);
            b.ChooseScaleMode(mode);
            if(mode != kTonicOnlyMode)
                CHECK(a.state().steps != b.state().steps); // two different patterns
            for(Sequencer* sequencer : {&a, &b}) {
                sequencer->SetAlgorithm(3);
                sequencer->SetMutation(100);
                sequencer->SetLength(length);
            }
            const Steps stored_a = a.state().steps, stored_b = b.state().steps;
            const std::vector<int>& list = kLists[static_cast<std::size_t>(mode)];
            const int span = std::min(static_cast<int>(list.size()), length);
            for(int tick = 0; tick < 48; ++tick) {
                const StepResult from_a = a.Tick(), from_b = b.Tick();
                const int position = tick % length;
                CHECK(from_a.sounds && from_b.sounds && from_a.note == from_b.note);
                CHECK(from_a.note
                      == kA + list[static_cast<std::size_t>(position % span)] + 12 * kDefaultOctave);
            }
            CHECK(a.state().steps == stored_a && b.state().steps == stored_b);
        }
    std::printf("algorithm 3: plays the scale list, ignores the stored steps and keeps them\n");
}

// Algorithm 4 is silent on every second pass.
void TestAlgorithm4() {
    for(const int length : {16, 5, 1}) {
        Sequencer sequencer(3);
        sequencer.SetNoteProbability(100);
        sequencer.ChooseScaleMode(kTonicOnlyMode); // a note on every step
        sequencer.SetAlgorithm(4);
        sequencer.SetLength(length);
        sequencer.Restart();
        for(int pass = 0; pass < 8; ++pass)
            for(int step = 0; step < length; ++step) {
                const StepResult result = sequencer.Tick();
                CHECK(sequencer.state().position == step);
                CHECK(result.sounds == (pass % 2 == 0));
                CHECK(sequencer.state().loop_on == (pass % 2 == 0 ? 1 : 0));
            }
    }
    std::printf("algorithm 4: exactly every second pass is silent\n");
}

// Algorithm 5 works on the step at the playhead.
void TestAlgorithm5() {
    std::printf("algorithm 5:\n");
    const struct {
        int mutation;
        double chance;
    } gentle[] = {{40, 0.20}, {41, 0.20}, {50, 0.25}, {1, 0.0}};
    for(const auto& c : gentle) {
        Sequencer sequencer(13 + static_cast<std::uint64_t>(c.mutation));
        sequencer.ChooseScaleMode(1);
        sequencer.SetAlgorithm(5);
        sequencer.SetMutation(c.mutation);
        const int ticks = 200000;
        double expected = 0.0;
        long long changed = 0;
        for(int tick = 0; tick < ticks; ++tick) {
            const Steps before = sequencer.state().steps;
            sequencer.Tick();
            const Steps& after = sequencer.state().steps;
            const std::size_t playhead = sequencer.state().position;
            for(std::size_t i = 0; i < after.size(); ++i) {
                CHECK((before[i] == kRest) == (after[i] == kRest)); // the rhythm never changes
                CHECK(i == playhead || before[i] == after[i]);
            }
            if(before[playhead] != kRest)
                expected += c.chance * (1.0 - NoteChance(1, before[playhead] - kA));
            changed += before[playhead] != after[playhead];
        }
        char name[64];
        std::snprintf(name, sizeof(name), "mutation %d %%: pitches changed, rhythm kept",
                      c.mutation);
        CheckCount(name, changed, expected);
    }
    {
        Sequencer sequencer(17);
        sequencer.ChooseScaleMode(1);
        sequencer.SetAlgorithm(5);
        sequencer.SetMutation(80);
        const int ticks = 200000;
        double expected = 0.0;
        long long changed = 0, rhythm = 0;
        for(int tick = 0; tick < ticks; ++tick) {
            const Steps before = sequencer.state().steps;
            sequencer.Tick();
            const Steps& after = sequencer.state().steps;
            const std::size_t playhead = sequencer.state().position;
            for(std::size_t i = 0; i < after.size(); ++i)
                CHECK(i == playhead || before[i] == after[i]);
            expected += 0.30 * (1.0 - DrawChance(1, 65, kA, before[playhead]));
            changed += before[playhead] != after[playhead];
            rhythm += (before[playhead] == kRest) != (after[playhead] == kRest);
        }
        CheckCount("mutation 80 %: steps redrawn, rests included", changed, expected);
        CHECK(rhythm > 0);
    }
}

// Advance, then mutate, then fetch: what a step's mutation wrote is what the
// same step plays.
void TestOrder() {
    Sequencer sequencer(19);
    sequencer.ChooseScaleMode(1);
    sequencer.SetAlgorithm(5);
    sequencer.SetMutation(100);
    int heard_new = 0;
    std::size_t expected_position = 0;
    for(int tick = 0; tick < 5000; ++tick) {
        const Steps before = sequencer.state().steps;
        const StepResult result = sequencer.Tick();
        const State& state = sequencer.state();
        CHECK(state.position == expected_position); // the mutated step is the new one
        expected_position = (expected_position + 1) % kNumSteps;
        const std::int8_t step = state.steps[state.position];
        CHECK(result.sounds == (step != kRest));
        if(result.sounds)
            CHECK(result.note == step + 12 * kDefaultOctave);
        heard_new += result.sounds && step != before[state.position];
    }
    CHECK(heard_new > 500);
    std::printf("order: a step's mutation is heard in that same step (%d times in 5000)\n",
                heard_new);
}

// Clock divide, length, octave and restart.
void TestClock() {
    Sequencer sequencer(4);
    sequencer.SetNoteProbability(100);
    sequencer.ChooseScaleMode(kTonicOnlyMode);
    for(int index = 0; index < kNumClockDivides; ++index) {
        const int divide = kClockDivides[static_cast<std::size_t>(index)];
        sequencer.SetClockDivide(index);
        sequencer.SetLength(3);
        sequencer.Restart();
        for(int tick = 0; tick < 7 * divide; ++tick) {
            const StepResult result = sequencer.Tick();
            CHECK(result.sounds == (tick % divide == 0)); // rests in between
            CHECK(sequencer.state().position == (tick / divide) % 3);
        }
    }
    sequencer.SetClockDivide(0);
    sequencer.SetLength(kNumSteps);
    for(int octave = 0; octave <= kMaxOctave; ++octave) {
        sequencer.SetOctave(octave);
        const StepResult result = sequencer.Tick();
        CHECK(result.sounds && result.note == kA + 12 * octave);
    }
    sequencer.SetOctave(kDefaultOctave);

    // Mutation is tried on every sixteenth, also between the steps.
    sequencer.SetClockDivide(3);
    sequencer.SetNoteProbability(50);
    sequencer.SetMutation(100);
    sequencer.Restart();
    int between = 0;
    for(int tick = 0; tick < 400; ++tick) {
        const Steps before = sequencer.state().steps;
        sequencer.Tick();
        between += sequencer.state().divide_count != 0 && sequencer.state().steps != before;
    }
    CHECK(between > 50);

    // A shorter length takes effect at the next advance, wherever the playhead is.
    sequencer.SetClockDivide(0);
    sequencer.SetMutation(0);
    sequencer.Restart();
    for(int tick = 0; tick < 13; ++tick)
        sequencer.Tick();
    CHECK(sequencer.state().position == 12);
    sequencer.SetLength(4);
    sequencer.Tick();
    CHECK(sequencer.state().position == 1); // (12 + 1) mod 4
    sequencer.Tick();
    sequencer.Restart();
    CHECK(sequencer.state().position == 0 && sequencer.state().divide_count == 0
          && sequencer.state().loop_on == 1);
    sequencer.Tick();
    CHECK(sequencer.state().position == 0); // the first tick plays the first step
    sequencer.Tick();
    CHECK(sequencer.state().position == 1);
    std::printf("clock: divide 1 to 64 rests between steps, length, octave, restart on step 1\n");
}

// The law of the mutation knob.
void TestMutationLaw() {
    CHECK(MutationPercent(0.0) == 0 && MutationPercent(0.25) == 10 && MutationPercent(0.5) == 20
          && MutationPercent(0.75) == 60 && MutationPercent(1.0) == 100);
    CHECK(MutationPercent(-1.0) == 0 && MutationPercent(2.0) == 100
          && MutationPercent(std::numeric_limits<double>::quiet_NaN()) == 0);
    int previous = 0;
    for(int i = 0; i <= 1000; ++i) {
        const int percent = MutationPercent(i / 1000.0);
        CHECK(percent >= previous && percent <= 100);
        previous = percent;
    }
    for(int percent = 0; percent <= 100; ++percent)
        CHECK(MutationPercent(MutationPosition(percent)) == percent);
    std::printf("mutation knob: first half 0 to 20 %%, second half 20 to 100 %%\n");
}

// A saved state continues exactly; a damaged one is refused.
void TestState() {
    Sequencer original(23);
    original.SetAlgorithm(2);
    original.SetMutation(60);
    original.SetClockDivide(1);
    original.SetLength(11);
    original.SetTonic(3);
    for(int tick = 0; tick < 137; ++tick)
        original.Tick();
    Sequencer copy(99);
    CHECK(copy.Load(original.state()));
    for(int tick = 0; tick < 2000; ++tick) {
        const StepResult a = original.Tick(), b = copy.Tick();
        CHECK(a.sounds == b.sounds && (!a.sounds || a.note == b.note));
        CHECK(original.state().steps == copy.state().steps
              && original.state().random == copy.state().random
              && original.state().position == copy.state().position);
    }
    const State good = copy.state();
    CHECK(IsValid(good));
    const auto refused = [&](void (*damage)(State&)) {
        State state = good;
        damage(state);
        const bool loaded = copy.Load(state);
        return !loaded && copy.state().steps == good.steps && copy.state().random == good.random
               && copy.state().tonic == good.tonic;
    };
    CHECK(refused([](State& s) { s.tonic = kNumTonics; }));
    CHECK(refused([](State& s) { s.scale_mode = kNumScaleModes; }));
    CHECK(refused([](State& s) { s.algorithm = 0; }));
    CHECK(refused([](State& s) { s.algorithm = kNumAlgorithms + 1; }));
    CHECK(refused([](State& s) { s.mutation = 101; }));
    CHECK(refused([](State& s) { s.note_probability = 101; }));
    CHECK(refused([](State& s) { s.octave = kMaxOctave + 1; }));
    CHECK(refused([](State& s) { s.length = 0; }));
    CHECK(refused([](State& s) { s.length = kNumSteps + 1; }));
    CHECK(refused([](State& s) { s.clock_divide = kNumClockDivides; }));
    CHECK(refused([](State& s) { s.position = kNumSteps; }));
    CHECK(refused([](State& s) { s.divide_count = kClockDivides[s.clock_divide]; }));
    CHECK(refused([](State& s) { s.loop_on = 2; }));
    CHECK(refused([](State& s) { s.at_start = 2; }));
    CHECK(refused([](State& s) { s.steps[7] = 97; }));
    CHECK(refused([](State& s) { s.steps[0] = -25; }));
    std::printf("state: a loaded state continues identically; damaged states refused\n");
}

} // namespace

int main() {
    TestStartPattern();
    TestDraws();
    TestRedraw();
    TestTonic();
    TestAlgorithm1();
    TestAlgorithm2();
    TestAlgorithm3();
    TestAlgorithm4();
    TestAlgorithm5();
    TestOrder();
    TestClock();
    TestMutationLaw();
    TestState();
    std::printf("generative sequencer: all tests passed\n");
    return 0;
}
