#include "generative_sequencer.h"

#include <algorithm>
#include <cmath>

namespace taureon::generative {
namespace {

constexpr std::array<Scale, kNumScaleModes> kScales = {{
    {8, {0, 2, 4, 5, 7, 9, 11, 0}}, // major
    {8, {0, 2, 3, 5, 7, 8, 10, 0}}, // minor
    {6, {0, 3, 5, 7, 10, 0}},       // pentatonic
    {8, {-2, 0, 1, 13, 0, 7, 8, 8}},
    {3, {0, 12, 0}},    // octaves
    {4, {0, 7, 12, 0}}, // fifths
    {1, {0}},           // the tonic alone
}};

// Stored notes stay within this range whatever the rules do; a saved state
// outside it is damaged.
constexpr int kLowestStep = -24;
constexpr int kHighestStep = 96;

// The knob position at which the two halves of the mutation law meet.
constexpr double kMutationKnee = 0.5;
constexpr int kMutationAtKnee = 20;

int Clamp(int value, int low, int high) {
    return std::min(std::max(value, low), high);
}

} // namespace

const Scale& ScaleFor(int mode) {
    return kScales[static_cast<std::size_t>(Clamp(mode, 0, kNumScaleModes - 1))];
}

int MutationPercent(double position) {
    if(!(position > 0.0))
        return 0; // also for NaN
    if(position >= 1.0)
        return 100;
    const double percent =
        position < kMutationKnee
            ? position / kMutationKnee * kMutationAtKnee
            : kMutationAtKnee
                  + (position - kMutationKnee) / (1.0 - kMutationKnee) * (100 - kMutationAtKnee);
    return Clamp(static_cast<int>(std::lround(percent)), 0, 100);
}

double MutationPosition(int percent) {
    const int p = Clamp(percent, 0, 100);
    if(p <= kMutationAtKnee)
        return kMutationKnee * p / kMutationAtKnee;
    return kMutationKnee
           + (1.0 - kMutationKnee) * (p - kMutationAtKnee) / (100 - kMutationAtKnee);
}

bool IsValid(const State& state) {
    for(const std::int8_t step : state.steps)
        if(step != kRest && (step < kLowestStep || step > kHighestStep))
            return false;
    return state.tonic < kNumTonics && state.scale_mode < kNumScaleModes && state.algorithm >= 1
           && state.algorithm <= kNumAlgorithms && state.mutation <= 100
           && state.note_probability <= 100 && state.octave <= kMaxOctave && state.length >= 1
           && state.length <= kNumSteps && state.clock_divide < kNumClockDivides
           && state.position < kNumSteps
           && state.divide_count < kClockDivides[state.clock_divide] && state.loop_on <= 1
           && state.at_start <= 1;
}

Sequencer::Sequencer(std::uint64_t seed) {
    state_.random = seed;
    Next(); // one step away from the seed itself
    const auto tonic = static_cast<std::int8_t>(state_.tonic);
    for(std::int8_t& step : state_.steps)
        step = Chance(state_.note_probability) ? tonic : kRest;
}

// PCG32 (O'Neill): a 64-bit linear congruential state, 32 bits out.
std::uint32_t Sequencer::Next() {
    const std::uint64_t old = state_.random;
    state_.random = old * 6364136223846793005ULL + 1442695040888963407ULL;
    const auto shifted = static_cast<std::uint32_t>(((old >> 18) ^ old) >> 27);
    const auto rotation = static_cast<std::uint32_t>(old >> 59);
    return (shifted >> rotation) | (shifted << ((32u - rotation) & 31u));
}

// Uniform in 0 .. bound - 1, without the bias a plain remainder would have.
std::uint32_t Sequencer::Below(std::uint32_t bound) {
    std::uint64_t product = static_cast<std::uint64_t>(Next()) * bound;
    auto low = static_cast<std::uint32_t>(product);
    if(low < bound) {
        const std::uint32_t threshold = (0u - bound) % bound;
        while(low < threshold) {
            product = static_cast<std::uint64_t>(Next()) * bound;
            low = static_cast<std::uint32_t>(product);
        }
    }
    return static_cast<std::uint32_t>(product >> 32);
}

bool Sequencer::Chance(int percent) {
    return Below(100) < static_cast<std::uint32_t>(Clamp(percent, 0, 100));
}

// The octave a drawn note lands in. The tonic-only mode does not spread.
int Sequencer::Spread() {
    return state_.scale_mode == kTonicOnlyMode ? 0 : static_cast<int>(Below(kOctaveSpread));
}

std::int8_t Sequencer::DrawNote() {
    const Scale& scale = kScales[state_.scale_mode];
    const int octave = Spread();
    const int entry = Chance(kTonicChance)
                          ? 0
                          : scale.entries[Below(static_cast<std::uint32_t>(scale.size))];
    return static_cast<std::int8_t>(state_.tonic + 12 * octave + entry);
}

std::int8_t Sequencer::Draw() {
    return Chance(state_.note_probability) ? DrawNote() : kRest;
}

void Sequencer::Restart() {
    state_.position = 0;
    state_.divide_count = 0;
    state_.loop_on = 1;
    state_.at_start = 1;
}

void Sequencer::Advance() {
    if(state_.at_start) { // the first tick after a restart plays the first step
        state_.at_start = 0;
        return;
    }
    state_.divide_count = static_cast<std::uint8_t>((state_.divide_count + 1)
                                                    % kClockDivides[state_.clock_divide]);
    if(state_.divide_count != 0)
        return;
    state_.position = static_cast<std::uint8_t>((state_.position + 1) % state_.length);
    if(state_.position == 0)
        state_.loop_on ^= 1;
}

// One of all sixteen steps, whatever the length: steps beyond the length keep
// changing unheard.
void Sequencer::RedrawOneOfAll() {
    if(Chance(state_.mutation))
        state_.steps[Below(kNumSteps)] = Draw();
}

// Three notes in a row, walking up the scale list in steps of one or two
// entries. Past the end of the list the walk continues an octave higher.
void Sequencer::WriteRun() {
    const Scale& scale = kScales[state_.scale_mode];
    const std::uint32_t slot = Below(kNumSteps);
    const int start = static_cast<int>(Below(static_cast<std::uint32_t>(scale.size)));
    const int stride = 1 + static_cast<int>(Below(2));
    const int base = state_.tonic + 12 * Spread();
    for(int i = 0; i < 3; ++i) {
        const int index = start + i * stride;
        const int note = base + scale.entries[static_cast<std::size_t>(index % scale.size)]
                         + (index >= scale.size ? 12 : 0);
        state_.steps[(slot + static_cast<std::uint32_t>(i)) % kNumSteps] =
            static_cast<std::int8_t>(note);
    }
}

void Sequencer::Mutate() {
    switch(state_.algorithm) {
        case 1:
        case 4: RedrawOneOfAll(); break;
        case 2:
            RedrawOneOfAll();
            // Mutation times note probability, as one chance.
            if(Below(10000) < static_cast<std::uint32_t>(state_.mutation) * state_.note_probability)
                WriteRun();
            break;
        case 5: {
            // Works on the step at the playhead. Up to 50 % it only changes
            // pitches, so the rhythm stays; above, it redraws, rests included.
            std::int8_t& step = state_.steps[state_.position];
            if(state_.mutation <= 50) {
                if(Chance(state_.mutation / 2) && step != kRest)
                    step = DrawNote();
            } else if(Chance(state_.mutation - 50)) {
                step = Draw();
            }
            break;
        }
        default: break; // algorithm 3 does not mutate
    }
}

StepResult Sequencer::Fetch() const {
    if(state_.divide_count != 0)
        return {false, 0};
    const int raise = 12 * state_.octave;
    if(state_.algorithm == 3) {
        // Reads the scale list itself; the stored steps are kept but not heard.
        const Scale& scale = kScales[state_.scale_mode];
        const int span = std::min<int>(scale.size, state_.length);
        return {true, state_.tonic
                          + scale.entries[static_cast<std::size_t>(state_.position % span)]
                          + raise};
    }
    if(state_.algorithm == 4 && !state_.loop_on)
        return {false, 0}; // every second pass is silent
    const std::int8_t step = state_.steps[state_.position];
    if(step == kRest)
        return {false, 0};
    return {true, step + raise};
}

StepResult Sequencer::Tick() {
    Advance();
    Mutate();
    return Fetch();
}

void Sequencer::ChooseScaleMode(int mode) {
    state_.scale_mode = static_cast<std::uint8_t>(Clamp(mode, 0, kNumScaleModes - 1));
    bool any = false;
    for(std::int8_t& step : state_.steps) {
        step = Draw();
        any = any || step != kRest;
    }
    // A pattern without a single note gets one, unless notes are switched off.
    if(!any && state_.note_probability > 0)
        state_.steps[0] =
            static_cast<std::int8_t>(state_.tonic + 12 * (Spread() + 1));
}

void Sequencer::SetTonic(int semitone) {
    const auto tonic = static_cast<std::uint8_t>(Clamp(semitone, 0, kNumTonics - 1));
    const int shift = tonic - state_.tonic;
    state_.tonic = tonic;
    for(std::int8_t& step : state_.steps)
        if(step != kRest)
            step = static_cast<std::int8_t>(Clamp(step + shift, kLowestStep, kHighestStep));
}

void Sequencer::SetAlgorithm(int algorithm) {
    state_.algorithm = static_cast<std::uint8_t>(Clamp(algorithm, 1, kNumAlgorithms));
}

void Sequencer::SetMutation(int percent) {
    state_.mutation = static_cast<std::uint8_t>(Clamp(percent, 0, 100));
}

void Sequencer::SetNoteProbability(int percent) {
    state_.note_probability = static_cast<std::uint8_t>(Clamp(percent, 0, 100));
}

void Sequencer::SetOctave(int octave) {
    state_.octave = static_cast<std::uint8_t>(Clamp(octave, 0, kMaxOctave));
}

void Sequencer::SetLength(int length) {
    state_.length = static_cast<std::uint8_t>(Clamp(length, 1, kNumSteps));
}

void Sequencer::SetClockDivide(int index) {
    state_.clock_divide = static_cast<std::uint8_t>(Clamp(index, 0, kNumClockDivides - 1));
    // Keep the count inside the new step, so that it still wraps.
    state_.divide_count %= kClockDivides[state_.clock_divide];
}

bool Sequencer::Load(const State& state) {
    if(!IsValid(state))
        return false;
    state_ = state;
    return true;
}

} // namespace taureon::generative
