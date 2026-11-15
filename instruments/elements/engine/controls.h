// The controls of the Elements instrument engine and what their values mean.
// Shared by the engine, the plug-in's parameters and the tests; free of DSP
// and VST3 types.
//
// Every control takes a value from 0 to 1. Most of them are the pots of the
// hardware and reach the DSP as they are. The rest belongs to playing the
// engine from a keyboard instead of from control voltages.
#pragma once

#include <cmath>

namespace elements_synth {

enum class Control : unsigned {
    // Exciters.
    Contour = 0,   // envelope shape of bow and blow
    BowLevel,
    BowTimbre,
    BlowLevel,
    BlowFlow,      // the hardware's FLOW: which air noise
    BlowTimbre,
    StrikeLevel,
    StrikeMallet,  // the hardware's MALLET: which strike
    StrikeTimbre,
    // Resonator.
    Coarse,        // whole semitones
    Fine,
    Geometry,
    Brightness,
    Damping,
    Position,
    // Space.
    Space,
    Freeze,        // on from 0.5: what the hardware does with SPACE pushed up by a voltage
    // Playing.
    Velocity,      // how much the key velocity moves the hardware's STRENGTH
    Volume,
    Count
};
constexpr unsigned kControlCount = static_cast<unsigned>(Control::Count);

inline double ControlClamp01(double value) {
    return value >= 0.0 ? (value <= 1.0 ? value : 1.0) : 0.0; // NaN becomes 0
}

// Coarse covers five octaves like the hardware's pot, centred on the played
// note, in whole semitones.
constexpr int kCoarseRange = 30; // semitones up and down
constexpr int kCoarseSteps = 2 * kCoarseRange; // the VST3 step count

inline int CoarseSemitones(double normalized) {
    const int step = static_cast<int>(ControlClamp01(normalized) * (kCoarseSteps + 1));
    return (step < kCoarseSteps ? step : kCoarseSteps) - kCoarseRange;
}

inline double CoarseNormalized(int semitones) {
    const int clamped = semitones < -kCoarseRange ? -kCoarseRange
                                                  : (semitones > kCoarseRange ? kCoarseRange : semitones);
    return static_cast<double>(clamped + kCoarseRange) / kCoarseSteps;
}

// Fine covers two semitones up and down, the range of the hardware's pot.
constexpr double kFineRange = 2.0;

inline double FineSemitones(double normalized) {
    return (2.0 * ControlClamp01(normalized) - 1.0) * kFineRange;
}

inline double FineNormalized(double semitones) {
    return ControlClamp01((semitones / kFineRange + 1.0) * 0.5);
}

inline bool ControlIsOn(double normalized) {
    return ControlClamp01(normalized) >= 0.5;
}

// The pitch wheel.
constexpr double kPitchBendRange = 2.0; // semitones up and down

// The hardware's STRENGTH, 0 to 1, for a key velocity of 1 to 127. At 0.5,
// where the hardware rests with nothing patched, the exciters have their
// nominal level; the range is 15 dB up and down. `amount` is the Velocity
// control: at 0 every note has the nominal level.
inline double StrengthFor(unsigned velocity, double amount) {
    const double v = (velocity > 127 ? 127.0 : static_cast<double>(velocity)) / 127.0;
    return ControlClamp01(0.5 + ControlClamp01(amount) * (v - 0.5));
}

// Defaults: the values of Part::Init() where the hardware has one.
constexpr double kControlDefaults[kControlCount] = {
    1.0,  // Contour
    0.0,  // BowLevel
    0.5,  // BowTimbre
    0.0,  // BlowLevel
    0.5,  // BlowFlow
    0.5,  // BlowTimbre
    0.8,  // StrikeLevel
    0.5,  // StrikeMallet
    0.5,  // StrikeTimbre
    0.5,  // Coarse: 0 semitones
    0.5,  // Fine: 0 semitones
    0.2,  // Geometry
    0.5,  // Brightness
    0.25, // Damping
    0.3,  // Position
    0.5,  // Space
    0.0,  // Freeze
    0.5,  // Velocity
    1.0,  // Volume
};

} // namespace elements_synth
