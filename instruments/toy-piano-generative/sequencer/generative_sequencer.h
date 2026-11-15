// A generative step sequencer: sixteen steps that are drawn at random from a
// scale and keep rewriting themselves by rule. Plain C++ without a plug-in
// framework, without allocation and without global state, so that the same
// class runs in a plug-in, in a host test and on a microcontroller.
//
// The rules are described in ../README.md. One call of Tick() is one
// sixteenth: advance, then mutate, then fetch the note.
#pragma once

#include <array>
#include <cstdint>

namespace taureon::generative {

constexpr int kNumSteps = 16;
// A step that holds no note.
constexpr std::int8_t kRest = -128;

// The tonic is one of the seven natural notes.
constexpr int kNumTonics = 7;
constexpr std::array<std::int8_t, kNumTonics> kTonicSemitones = {0, 2, 4, 5, 7, 9, 11};
constexpr int kDefaultTonic = 5; // A

constexpr int kNumScaleModes = 7;
constexpr int kDefaultScaleMode = 1; // minor
constexpr int kTonicOnlyMode = 6;

constexpr int kNumAlgorithms = 5; // numbered 1 to 5
constexpr int kMaxOctave = 5;
constexpr int kDefaultOctave = 2;
constexpr int kDefaultNoteProbability = 65; // percent

// One step lasts this many sixteenths.
constexpr int kNumClockDivides = 9;
constexpr std::array<std::uint8_t, kNumClockDivides> kClockDivides = {1, 2,  3,  4, 6,
                                                                      8, 16, 32, 64};

// Fixed properties of a draw: how often it is the tonic itself, and over how
// many octaves it spreads.
constexpr int kTonicChance = 30; // percent
constexpr int kOctaveSpread = 2;

// What a scale mode draws from, in semitones above the tonic. Every list but
// the last ends with one more tonic, which makes the tonic more likely still.
struct Scale {
    int size;
    std::array<std::int8_t, 8> entries;
};
const Scale& ScaleFor(int mode);

// The law of the mutation knob: the first half of its travel covers 0 to
// 20 %, the second half 20 to 100 %, so that low values are easy to set.
int MutationPercent(double position);
double MutationPosition(int percent);

// Everything the sequencer is. Saving and restoring this continues the
// pattern exactly where it was.
struct State {
    // Each step: tonic + 12 * (0 or 1) + scale entry, or kRest. The octave
    // setting is added when a step is played, not stored.
    std::array<std::int8_t, kNumSteps> steps{};
    std::uint8_t tonic = kDefaultTonic;                 // index into kTonicSemitones
    std::uint8_t scale_mode = kDefaultScaleMode;        // 0 to 6
    std::uint8_t algorithm = 1;                         // 1 to 5
    std::uint8_t mutation = 0;                          // percent
    std::uint8_t note_probability = kDefaultNoteProbability; // percent
    std::uint8_t octave = kDefaultOctave;               // 0 to 5
    std::uint8_t length = kNumSteps;                    // 1 to 16
    std::uint8_t clock_divide = 0;                      // index into kClockDivides
    std::uint8_t position = 0;                          // the step at the playhead
    std::uint8_t divide_count = 0;                      // sixteenths into the step
    std::uint8_t loop_on = 1;                           // toggles with every pass
    std::uint8_t at_start = 1;                          // the next tick plays step 1
    std::uint64_t random = 0;                           // state of the generator
};

// True if every field is in its range.
bool IsValid(const State& state);

struct StepResult {
    bool sounds;
    int note; // tonic + 12 * spread + scale entry + 12 * octave; only if `sounds`
};

class Sequencer {
  public:
    // Draws the start-up pattern: every step is the tonic or a rest.
    explicit Sequencer(std::uint64_t seed);

    // One sixteenth: advance, mutate, fetch.
    StepResult Tick();
    // Back to the first step. The pattern and the generator stay as they are.
    void Restart();

    // Redraws all sixteen steps from the new mode, also if it is the old one.
    void ChooseScaleMode(int mode);
    // Transposes the stored notes by the difference; rests stay rests.
    void SetTonic(int index);
    // None of these touch the stored steps.
    void SetAlgorithm(int algorithm);
    void SetMutation(int percent);
    void SetNoteProbability(int percent);
    void SetOctave(int octave);
    void SetLength(int length);
    void SetClockDivide(int index);

    const State& state() const { return state_; }
    // Takes over a saved state. An invalid one is refused and changes nothing.
    bool Load(const State& state);

  private:
    std::uint32_t Next();
    std::uint32_t Below(std::uint32_t bound);
    bool Chance(int percent);
    int Spread();
    std::int8_t DrawNote();
    std::int8_t Draw();
    void Advance();
    void Mutate();
    void RedrawOneOfAll();
    void WriteRun();
    StepResult Fetch() const;

    State state_;
};

} // namespace taureon::generative
