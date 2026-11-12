// Parameters of Toy Piano Classic and their mapping to engine values. Shared by
// the processor, the controller, the editor and the tests; free of VST3 types.
#pragma once

#include "toy_piano.h"

#include <cmath>
#include <cstdint>

namespace taureon::toy_piano_classic {

// IDs are stored in automation data and in saved projects: never renumber.
enum ParamId : std::uint32_t {
    kParamMasterPitch = 100,
    kParamAttack = 101,
    kParamDecay = 102,
    kParamVolume = 103,
    kParamPitchBend = 104, // fed by the MIDI pitch wheel; not saved
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

// Defaults as normalized values (0..1). Attack and decay use the position of
// the firmware's pots VR1 and VR2: 0 is fully left.
constexpr double kDefaultMasterPitch = 0.5; // 0 semitones
constexpr double kDefaultAttack = 0.0;      // 2 ms
constexpr double kDefaultDecay = 0.6;       // about 580 ms
constexpr double kDefaultVolume =           // 0 dB: the level of the firmware
    -kVolumeMinDb / (kVolumeMaxDb - kVolumeMinDb);
constexpr double kDefaultPitchBend = 0.5; // wheel centred

inline double Clamp01(double value) {
    return value >= 0.0 ? (value <= 1.0 ? value : 1.0) : 0.0; // NaN becomes 0
}

// The step a normalized value falls on, by the rule VST3 defines for stepped
// parameters. Hosts and the SDK's parameter display use the same rule.
inline int MasterPitchSemitones(double normalized) {
    const int step = static_cast<int>(Clamp01(normalized) * (kMasterPitchSteps + 1));
    return (step < kMasterPitchSteps ? step : kMasterPitchSteps) - kMasterPitchRange;
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

// Layout of the saved state: version, then master pitch, attack, decay and
// volume as normalized doubles.
constexpr std::int32_t kStateVersion = 1;

} // namespace taureon::toy_piano_classic
