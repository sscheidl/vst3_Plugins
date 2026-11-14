// Parameters of Torus Classic and their mapping to engine values. Shared by
// the processor, the controller, the editor and the tests; free of VST3 types.
#pragma once

#include <cmath>
#include <cstdint>

namespace torus_classic {

// IDs are stored in automation data and in saved projects: never renumber.
enum ParamId : std::uint32_t {
    kParamTune = 100,
    kParamStructure = 101,
    kParamBrightness = 102,
    kParamDamping = 103,
    kParamPosition = 104,
    kParamExcitation = 105,
    kParamModel = 106,
    kParamVolume = 107,
    kParamPitchBend = 108, // fed by the MIDI pitch wheel; not saved
};
constexpr std::uint32_t kFirstParam = kParamTune;
constexpr int kParamCount = 9;
constexpr int kSavedParamCount = 8; // 100..107

// MIDI controllers the host routes to the parameters, on every channel: the
// numbers of the Daisy firmware. Its reverb controllers 26, 27, 30, 31, 32 and
// its polyphony controller 29 have no counterpart here.
constexpr int kMidiCcTune = 20;
constexpr int kMidiCcStructure = 21;
constexpr int kMidiCcBrightness = 22;
constexpr int kMidiCcDamping = 23;
constexpr int kMidiCcPosition = 24;
constexpr int kMidiCcExcitation = 25;
constexpr int kMidiCcModel = 28;
constexpr int kMidiCcVolume = 33;

constexpr double kTuneRange = 24.0;      // semitones up and down, continuous
constexpr double kPitchBendRange = 2.0;  // semitones up and down
constexpr int kModelCount = 6;
constexpr int kModelSteps = kModelCount - 1; // the VST3 step count

// In the order of torus::ResonatorModel. The last one is the dry synthesis
// branch of the firmware's "String & Reverb"; here it has no reverb, hence
// the name.
constexpr const char* kModelNames[kModelCount] = {
    "Modal Resonator", "Sympathetic Strings", "Inharmonic Strings",
    "FM Voice",        "Western Chords",      "Stereo Strings"};

// Defaults as normalized values (0..1): the firmware's.
constexpr double kDefaultTune = 0.5; // 0 semitones
constexpr double kDefaultStructure = 0.5;
constexpr double kDefaultBrightness = 0.5;
constexpr double kDefaultDamping = 0.5;
constexpr double kDefaultPosition = 0.5;
constexpr double kDefaultExcitation = 0.8;
constexpr double kDefaultModel = 0.0;     // Modal Resonator
constexpr double kDefaultVolume = 1.0;    // 100 %: the firmware's level
constexpr double kDefaultPitchBend = 0.5; // wheel centred

constexpr double kDefaults[kParamCount] = {
    kDefaultTune,     kDefaultStructure,  kDefaultBrightness, kDefaultDamping, kDefaultPosition,
    kDefaultExcitation, kDefaultModel,    kDefaultVolume,     kDefaultPitchBend};

inline double Clamp01(double value) {
    return value >= 0.0 ? (value <= 1.0 ? value : 1.0) : 0.0; // NaN becomes 0
}

// The model a normalized value stands for, by the rule VST3 defines for
// stepped parameters. Hosts, the parameter's display, its text input and the
// engine all use this one function.
inline int ModelIndex(double normalized) {
    const int step = static_cast<int>(Clamp01(normalized) * (kModelSteps + 1));
    return step < kModelSteps ? step : kModelSteps;
}

inline double ModelNormalized(int index) {
    const int clamped = index < 0 ? 0 : (index > kModelSteps ? kModelSteps : index);
    return static_cast<double>(clamped) / kModelSteps;
}

inline double TuneSemitones(double normalized) {
    return (2.0 * Clamp01(normalized) - 1.0) * kTuneRange;
}

inline double TuneNormalized(double semitones) {
    return Clamp01((semitones / kTuneRange + 1.0) * 0.5);
}

// The pitch wheel as the firmware knows it: 14 bits, 8192 in the middle.
constexpr int kPitchBendCentreRaw = 8192;
constexpr int kPitchBendMaxRaw = 16383;

inline int PitchBendRaw(double normalized) {
    return static_cast<int>(std::lround(Clamp01(normalized) * kPitchBendMaxRaw));
}

// The firmware's bend curve (config.h, Bend): -2 at 0, 0 at 8192, +2 at 16383.
// The two halves differ by one step, so the centre is exactly 0.
inline double PitchBendSemitones(double normalized) {
    const int raw = PitchBendRaw(normalized);
    return raw <= kPitchBendCentreRaw
               ? (raw - kPitchBendCentreRaw) / 4096.0
               : (raw - kPitchBendCentreRaw) * (kPitchBendRange / 8191.0);
}

inline double PitchBendNormalized(double semitones) {
    if(!(semitones > -kPitchBendRange))
        return 0.0;
    if(semitones >= kPitchBendRange)
        return 1.0;
    const double raw = semitones <= 0.0
                           ? kPitchBendCentreRaw + semitones * 4096.0
                           : kPitchBendCentreRaw + semitones * (8191.0 / kPitchBendRange);
    return Clamp01(std::round(raw) / kPitchBendMaxRaw);
}

// Layout of the saved state: version, then the normalized values of the
// parameters 100..107 as doubles, in that order.
constexpr std::int32_t kStateVersion = 1;
// Layout of the controller's own state: version, the editor zoom as a double,
// the keyboard as one byte (1 for shown).
constexpr std::int32_t kControllerStateVersion = 1;

// The message that carries a note struck on the screen keyboard from the
// controller to the processor.
constexpr const char* kNoteMessageId = "TaureonTorusClassicNote";
constexpr const char* kNoteMessageNote = "note";
constexpr const char* kNoteMessageVelocity = "velocity";

} // namespace torus_classic
