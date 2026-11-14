// Parameters of the Elements plug-in and their mapping to engine values.
// Shared by the processor, the controller, the editor and the tests; free of
// VST3 types.
#pragma once

#include "controls.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace elements_synth {

// IDs are stored in automation data and in saved projects: never renumber.
// The first nineteen are the engine's controls, in the order of Control.
enum ParamId : std::uint32_t {
    kParamContour = 100,
    kParamBowLevel = 101,
    kParamBowTimbre = 102,
    kParamBlowLevel = 103,
    kParamBlowFlow = 104,
    kParamBlowTimbre = 105,
    kParamStrikeLevel = 106,
    kParamStrikeMallet = 107,
    kParamStrikeTimbre = 108,
    kParamCoarse = 109,
    kParamFine = 110,
    kParamGeometry = 111,
    kParamBrightness = 112,
    kParamDamping = 113,
    kParamPosition = 114,
    kParamSpace = 115,
    kParamFreeze = 116,
    kParamVelocity = 117,
    kParamVolume = 118,
    kParamPitchBend = 119, // fed by the MIDI pitch wheel; not saved
};
constexpr std::uint32_t kFirstParam = kParamContour;
constexpr int kParamCount = 20;

// The parameters a project stores, as indices (ID minus kFirstParam), in the
// order of the state file: all but the pitch wheel.
constexpr int kSavedParamCount = 19;
constexpr std::array<int, kSavedParamCount> kSavedIndex = {0,  1,  2,  3,  4,  5,  6,  7,  8, 9,
                                                           10, 11, 12, 13, 14, 15, 16, 17, 18};

constexpr int Index(ParamId id) {
    return static_cast<int>(id) - static_cast<int>(kFirstParam);
}

static_assert(kSavedParamCount == static_cast<int>(kControlCount));
static_assert(Index(kParamVolume) == static_cast<int>(Control::Volume));
static_assert(Index(kParamCoarse) == static_cast<int>(Control::Coarse));
static_assert(Index(kParamFreeze) == static_cast<int>(Control::Freeze));

// MIDI controllers the host routes to parameters, on every channel. Where
// Torus Classic has a parameter of the same meaning, the number is the same.
constexpr int kMidiCcGeometry = 21;
constexpr int kMidiCcBrightness = 22;
constexpr int kMidiCcDamping = 23;
constexpr int kMidiCcPosition = 24;
constexpr int kMidiCcVolume = 33;
constexpr int kMidiCcSpace = 91; // "effects 1 depth", the usual reverb amount

constexpr double kDefaultPitchBend = 0.5; // wheel centred

inline double DefaultOf(int index) {
    return index < static_cast<int>(kControlCount) ? kControlDefaults[index] : kDefaultPitchBend;
}

constexpr double kDefaults[kParamCount] = {
    kControlDefaults[0],  kControlDefaults[1],  kControlDefaults[2],  kControlDefaults[3],
    kControlDefaults[4],  kControlDefaults[5],  kControlDefaults[6],  kControlDefaults[7],
    kControlDefaults[8],  kControlDefaults[9],  kControlDefaults[10], kControlDefaults[11],
    kControlDefaults[12], kControlDefaults[13], kControlDefaults[14], kControlDefaults[15],
    kControlDefaults[16], kControlDefaults[17], kControlDefaults[18], kDefaultPitchBend};

inline double Clamp01(double value) {
    return ControlClamp01(value);
}

inline bool IsOn(double normalized) {
    return ControlIsOn(normalized);
}

// The pitch wheel as MIDI has it: 14 bits, 8192 in the middle. The two halves
// differ by one step, so the centre is exactly 0 semitones.
constexpr int kPitchBendCentreRaw = 8192;
constexpr int kPitchBendMaxRaw = 16383;

inline int PitchBendRaw(double normalized) {
    return static_cast<int>(std::lround(Clamp01(normalized) * kPitchBendMaxRaw));
}

inline double PitchBendSemitones(double normalized) {
    const int raw = PitchBendRaw(normalized);
    return raw <= kPitchBendCentreRaw
               ? (raw - kPitchBendCentreRaw) * (kPitchBendRange / 8192.0)
               : (raw - kPitchBendCentreRaw) * (kPitchBendRange / 8191.0);
}

inline double PitchBendNormalized(double semitones) {
    if(!(semitones > -kPitchBendRange))
        return 0.0;
    if(semitones >= kPitchBendRange)
        return 1.0;
    const double raw = semitones <= 0.0
                           ? kPitchBendCentreRaw + semitones * (8192.0 / kPitchBendRange)
                           : kPitchBendCentreRaw + semitones * (8191.0 / kPitchBendRange);
    return Clamp01(std::round(raw) / kPitchBendMaxRaw);
}

// Layout of the saved state, version 1: the normalized values of the
// parameters 100..118 as doubles, in that order.
constexpr std::int32_t kStateVersion = 1;
// Layout of the controller's own state: version, the editor zoom as a double,
// the keyboard as one byte (1 for shown).
constexpr std::int32_t kControllerStateVersion = 1;

// The message that carries a key of the screen keyboard from the controller
// to the processor. Velocity 1..127 is a key going down, 0 its release.
constexpr const char* kNoteMessageId = "TaureonElementsNote";
constexpr const char* kNoteMessageNote = "note";
constexpr const char* kNoteMessageVelocity = "velocity";

// The message with which the processor tells the controller, on activation,
// at which rates it runs: for the status line of the window.
constexpr const char* kStatusMessageId = "TaureonElementsStatus";
constexpr const char* kStatusHostRate = "hostRate";     // float, Hz
constexpr const char* kStatusEngineRate = "engineRate"; // float, Hz
constexpr const char* kStatusLatency = "latency";       // int, host samples

} // namespace elements_synth
