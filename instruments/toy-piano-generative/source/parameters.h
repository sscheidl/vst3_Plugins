// Parameters of Toy Piano Generative and their mapping to engine and
// sequencer values. Shared by the processor, the controller, the editor and
// the tests; free of VST3 types.
#pragma once

#include "generative_sequencer.h"
#include "toy_piano.h"

#include <cmath>
#include <cstdint>

namespace taureon::toy_piano_generative {

// IDs are stored in automation data and in saved projects: never renumber.
enum ParamId : std::uint32_t {
    // The sound, as in Toy Piano Classic.
    kParamMasterPitch = 100,
    kParamAttack = 101,
    kParamDecay = 102,
    kParamVolume = 103,
    kParamPitchBend = 104, // fed by the MIDI pitch wheel; not saved
    // The sequencer.
    kParamRun = 110,
    kParamAlgorithm = 111,
    kParamMutation = 112,
    kParamNoteProbability = 113,
    kParamScale = 114,
    kParamTonic = 115,
    kParamOctave = 116,
    kParamLength = 117,
    kParamClockDivide = 118,
    kParamTuning = 119,
    // Keys and MIDI notes transpose the sequence instead of playing a note.
    kParamKeysTranspose = 120,
    // Freezes the pattern: no mutation while it is on.
    kParamMutationLock = 121,
    // Mutation on every tick, or after a number of unchanged passes.
    kParamMutateEvery = 122,
};

// MIDI controllers the host routes to the parameters above, on every channel.
// Attack and decay use the numbers MIDI defines for these times (Sound
// Controllers 4 and 6); for a transposition MIDI defines none.
constexpr int kMidiCcMasterPitch = 16; // General Purpose Controller 1
constexpr int kMidiCcAttack = 73;
constexpr int kMidiCcDecay = 75;

// Master pitch in whole semitones.
constexpr int kMasterPitchRange = 24;
constexpr int kMasterPitchSteps = 2 * kMasterPitchRange;
// Pitch wheel range in semitones, up and down.
constexpr double kPitchBendRange = 2.0;
// Volume in dB. The lowest position is silence.
constexpr double kVolumeMinDb = -60.0;
constexpr double kVolumeMaxDb = 24.0;

// Steps of the stepped sequencer parameters: one less than the number of values.
constexpr int kAlgorithmSteps = generative::kNumAlgorithms - 1;
constexpr int kNoteProbabilitySteps = 100;
constexpr int kScaleSteps = generative::kNumScaleModes - 1;
constexpr int kTonicSteps = generative::kNumTonics - 1;
constexpr int kOctaveSteps = generative::kMaxOctave;
constexpr int kLengthSteps = generative::kNumSteps - 1;
constexpr int kClockDivideSteps = generative::kNumClockDivides - 1;
constexpr int kMutateEverySteps = generative::kMaxMutateEvery;

// Defaults as normalized values (0..1). Attack and decay use the position of
// the firmware's pots VR1 and VR2: 0 is fully left.
constexpr double kDefaultMasterPitch = 0.5; // 0 semitones
constexpr double kDefaultAttack = 0.0;      // 2 ms
constexpr double kDefaultDecay = 0.6;       // about 580 ms
constexpr double kDefaultVolume =           // 0 dB: the level of the firmware
    -kVolumeMinDb / (kVolumeMaxDb - kVolumeMinDb);
constexpr double kDefaultPitchBend = 0.5; // wheel centred
constexpr double kDefaultRun = 1.0;
constexpr double kDefaultAlgorithm = 0.0; // algorithm 1
constexpr double kDefaultMutation = 0.0;
constexpr double kDefaultNoteProbability =
    static_cast<double>(generative::kDefaultNoteProbability) / kNoteProbabilitySteps;
constexpr double kDefaultScale = static_cast<double>(generative::kDefaultScaleMode) / kScaleSteps;
constexpr double kDefaultTonic = static_cast<double>(generative::kDefaultTonic) / kTonicSteps;
constexpr double kDefaultOctave = static_cast<double>(generative::kDefaultOctave) / kOctaveSteps;
constexpr double kDefaultLength = 1.0;      // 16 steps
constexpr double kDefaultClockDivide = 0.0; // every sixteenth
constexpr double kDefaultTuning = 0.0;      // exact
constexpr double kDefaultKeysTranspose = 1.0;
constexpr double kDefaultMutationLock = 0.0;
constexpr double kDefaultMutateEvery = 0.0; // on every tick, as the rules say

// The five algorithms, by name, and what each does: shown as the value of
// the Algorithm parameter and as the tooltip over it.
struct AlgorithmInfo {
    const char* name;
    const char* description;
};
constexpr AlgorithmInfo kAlgorithms[generative::kNumAlgorithms] = {
    {"Drift",
     "Drift: now and then one of the 16 steps is drawn anew, also steps beyond the length. "
     "Mutation sets how often."},
    {"Runs",
     "Runs: like Drift, and now and then three steps become a run up the scale. Mutation and "
     "Notes set how often."},
    {"Ladder",
     "Ladder: plays the notes of the scale in order, one per step. The pattern is kept but not "
     "heard; nothing mutates."},
    {"Breathe",
     "Breathe: like Drift, but every second pass through the pattern is silent."},
    {"Focus",
     "Focus: changes only the step being played. Up to 50 % only its pitch, so the rhythm stays; "
     "above, it can also turn notes into rests and back."},
};

// The scales, in the order of the sequencer's modes.
constexpr const char* kScaleNames[generative::kNumScaleModes] = {
    "Major",       "Minor",      "Dorian",     "Phrygian",   "Lydian",
    "Mixolydian",  "Harm. Minor", "Mel. Minor", "Major Pent.", "Minor Pent.",
    "Blues",       "Whole Tone", "Hirajoshi",  "Odd",        "Octave",
    "Fifths",      "Tonic"};

// The sequencer's notes sound this many semitones higher than their numbers:
// its default register then lies in the Toy Piano's own, around its keys.
constexpr int kSequencerTranspose = 24;
// The velocity of a sequencer note: that of the firmware's keys.
constexpr std::uint8_t kSequencerVelocity = toy_piano::kMidiOutVelocity;

inline double Clamp01(double value) {
    return value >= 0.0 ? (value <= 1.0 ? value : 1.0) : 0.0; // NaN becomes 0
}

// The step a normalized value falls on, by the rule VST3 defines for stepped
// parameters. Hosts and the SDK's parameter display use the same rule.
inline int StepIndex(double normalized, int steps) {
    const int step = static_cast<int>(Clamp01(normalized) * (steps + 1));
    return step < steps ? step : steps;
}

inline double StepNormalized(int index, int steps) {
    return static_cast<double>(index < 0 ? 0 : (index > steps ? steps : index)) / steps;
}

// Number of steps of a parameter, 0 for a continuous one.
constexpr int ParamSteps(std::uint32_t id) {
    switch(id) {
        case kParamMasterPitch: return kMasterPitchSteps;
        case kParamRun:
        case kParamKeysTranspose:
        case kParamMutationLock: return 1;
        case kParamAlgorithm: return kAlgorithmSteps;
        case kParamNoteProbability: return kNoteProbabilitySteps;
        case kParamScale: return kScaleSteps;
        case kParamTonic: return kTonicSteps;
        case kParamOctave: return kOctaveSteps;
        case kParamLength: return kLengthSteps;
        case kParamClockDivide: return kClockDivideSteps;
        case kParamMutateEvery: return kMutateEverySteps;
        default: return 0;
    }
}

// A knob position moved onto the nearest value a stepped parameter can take.
inline double SnapToStep(std::uint32_t id, double normalized) {
    const int steps = ParamSteps(id);
    return steps > 0 ? StepNormalized(StepIndex(normalized, steps), steps) : Clamp01(normalized);
}

// The algorithm that plays the scale itself and uses neither Mutation nor Notes.
constexpr int kLadderAlgorithm = 3;

inline bool IsOn(double normalized) {
    return normalized >= 0.5;
}

inline int MasterPitchSemitones(double normalized) {
    return StepIndex(normalized, kMasterPitchSteps) - kMasterPitchRange;
}

inline double MasterPitchNormalized(int semitones) {
    return static_cast<double>(semitones + kMasterPitchRange) / kMasterPitchSteps;
}

inline double PitchBendSemitones(double normalized) {
    return (2.0 * Clamp01(normalized) - 1.0) * kPitchBendRange;
}

inline double VolumeDb(double normalized) {
    return kVolumeMinDb + Clamp01(normalized) * (kVolumeMaxDb - kVolumeMinDb);
}

inline double VolumeNormalized(double db) {
    return Clamp01((db - kVolumeMinDb) / (kVolumeMaxDb - kVolumeMinDb));
}

inline float VolumeGain(double normalized) {
    if(!(normalized > 0.0))
        return 0.0f;
    return static_cast<float>(std::pow(10.0, VolumeDb(normalized) / 20.0));
}

// The same logarithmic mapping the firmware applies to its pots.
inline float AttackSeconds(double normalized) {
    return toy_piano::LogTime(static_cast<float>(normalized), toy_piano::kAttackMin,
                              toy_piano::kAttackMax);
}

inline float DecaySeconds(double normalized) {
    return toy_piano::LogTime(static_cast<float>(normalized), toy_piano::kDecayMin,
                              toy_piano::kDecayMax);
}

// Inverse of LogTime, for typed-in values.
inline double LogTimePosition(double seconds, double minimum, double maximum) {
    if(seconds <= minimum)
        return 0.0;
    if(seconds >= maximum)
        return 1.0;
    return std::log(seconds / minimum) / std::log(maximum / minimum);
}

// The MIDI note a sequencer note sounds as. Notes beyond the MIDI range are
// folded back by octaves, so the rhythm stays whole.
inline std::uint8_t SoundingNote(int sequencer_note) {
    int note = sequencer_note + kSequencerTranspose;
    while(note > 127)
        note -= 12;
    while(note < 0)
        note += 12;
    return static_cast<std::uint8_t>(note);
}

// What a key means when keys transpose: the sequence's tonic sounds at that
// key. Keys below C1 or above B6 take the nearest octave the sequencer has.
struct KeyTransposition {
    int tonic;  // 0 to 11
    int octave; // 0 to 5
};

inline KeyTransposition TranspositionForKey(int note) {
    const int clamped = note < 0 ? 0 : (note > 127 ? 127 : note);
    const int octave = clamped / 12 - kSequencerTranspose / 12;
    return {clamped % 12,
            octave < 0 ? 0 : (octave > generative::kMaxOctave ? generative::kMaxOctave : octave)};
}

// The key at which a tonic sounds; the inverse of the above.
inline int KeyForTransposition(int tonic, int octave) {
    return tonic + 12 * octave + kSequencerTranspose;
}

// Layout of the controller's own state: version, the editor zoom as a double,
// the keyboard as one byte (1 for shown), and the keyboard's shift in
// semitones as one signed byte.
constexpr std::int32_t kControllerStateVersion = 1;

// The message that carries a note struck on the screen keyboard from the
// controller to the processor.
constexpr const char* kNoteMessageId = "TaureonToyPianoGenerativeNote";
constexpr const char* kNoteMessageNote = "note";
constexpr const char* kNoteMessageVelocity = "velocity";

} // namespace taureon::toy_piano_generative
