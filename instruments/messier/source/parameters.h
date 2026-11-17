// Parameters of Messier and what their values mean. Shared by the processor, the
// controller, the editor and the tests; free of VST3 types.
//
// Every parameter is a pot of the Daisy kit as a value from 0 to 1; the engine
// applies the firmware's laws to it, snap bands around the centre included. What
// the host shows is the value the law makes of it, computed here with the
// firmware's own functions (core/reference_dsp.h); typed text is converted back.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace messier {

// IDs are stored in automation data and in saved projects: never renumber.
// Kept free for 0.2.0: 111 (input gain), 116 (grain FM), 124..126 (Source,
// Record, Circular).
enum ParamId : std::uint32_t {
    kParamStart = 100,
    kParamSpeed = 101,
    kParamSize = 102,
    kParamDensity = 103,
    kParamPitch = 104,
    kParamOverlap = 105,
    kParamBlend = 106,
    kParamWindow = 107,
    kParamSpray = 110,        // secondary of Start
    kParamPanRandom = 112,    // secondary of Size
    kParamDisplacement = 113, // secondary of Density
    kParamPitchRandom = 114,  // secondary of Pitch
    kParamSizeRandom = 115,   // secondary of Overlap
    kParamMask = 117,         // secondary of Window
    kParamFreeze = 120,
    kParamReset = 121,        // a button: 0 to 1 sends playback back to START; not saved
    kParamVolume = 122,
    kParamPitchBend = 123,    // fed by the MIDI pitch wheel; not saved
};
constexpr int kParamCount = 18;
constexpr std::array<std::uint32_t, kParamCount> kParamIds = {
    100, 101, 102, 103, 104, 105, 106, 107, 110, 112, 113, 114, 115, 117, 120, 121, 122, 123};

// The position of a parameter in kParamIds, or -1 for an ID that is not one.
constexpr int Index(std::uint32_t id) {
    for(int i = 0; i < kParamCount; ++i)
        if(kParamIds[static_cast<std::size_t>(i)] == id)
            return i;
    return -1;
}

// The eight primaries and the secondaries: the secondary of pot i has ID 110 + i.
constexpr std::uint32_t kFirstPrimary = kParamStart;
constexpr std::uint32_t kFirstSecondary = 110;
constexpr int kPotCount = 8;
constexpr bool IsPrimary(std::uint32_t id) {
    return id >= kFirstPrimary && id < kFirstPrimary + kPotCount;
}
constexpr bool IsSecondary(std::uint32_t id) {
    return id >= kFirstSecondary && id < kFirstSecondary + kPotCount && Index(id) >= 0;
}

// The parameters a project stores, as indices into kParamIds, in the order of the
// state: all but Reset and the pitch wheel.
constexpr int kSavedParamCount = 16;
constexpr std::array<int, kSavedParamCount> kSavedIndex = {0, 1, 2,  3,  4,  5,  6,  7,
                                                           8, 9, 10, 11, 12, 13, 14, 16};

// MIDI controllers the host routes to the parameters, on every channel: those of
// the firmware (CC 20 to 27 for the pots in order) and 33 for Volume, as in
// Kekule. The secondaries have no controller, as in the firmware.
constexpr int kMidiCcFirstPot = 20;
constexpr int kMidiCcVolume = 33;

// Defaults as normalized values: the firmware's Parameters::primary, secondaries
// at 0, Volume at 100 %.
constexpr double kDefaultPitchBend = 0.5;
constexpr std::array<double, kParamCount> kDefaults = {
    0.0, 0.5, 1.0, 0.4, 0.5, 0.5, 0.0, 0.0, // Start .. Window
    0.0, 0.0, 0.0, 0.0, 0.0, 0.0,           // secondaries
    0.0,                                    // Freeze off
    0.0,                                    // Reset
    1.0,                                    // Volume
    kDefaultPitchBend};

inline double Clamp01(double value) {
    return value >= 0.0 ? (value <= 1.0 ? value : 1.0) : 0.0; // NaN becomes 0
}

// A switch or button as a normalized value: on from the middle up (VST3 step rule).
inline bool IsOn(double normalized) {
    return Clamp01(normalized) >= 0.5;
}

// --- The firmware's laws, in float as the engine computes them ---------------
// Copied from core/reference_dsp.cpp (KnobSpeed, LawSpeed, KnobPitch, LawPitch,
// LawDensity), so that the display shows what the engine plays. A test compares
// these with the core's functions.
namespace law {
inline float KnobSpeed(float p) { return p <= 0.5f ? p * 1.25f : 0.625f + (p - 0.5f) * 0.75f; }
inline float KnobPitch(float p) { return p <= 0.5f ? p * 1.20f : 0.600f + (p - 0.5f) * 0.80f; }
inline float Speed(float g) {
    float k = g * 8.0f - 4.0f;
    const float a = std::fabs(k);
    if(a >= 0.975f && a <= 1.025f)
        k = (k > 0.0f) ? 1.0f : -1.0f;
    return k;
}
inline float Pitch(float g) {
    const float k = 0.125f * std::exp(g * 3.4657359028f);
    return (k >= 0.995f && k <= 1.005f) ? 1.0f : k;
}
inline float Density(float g) {
    const float t = (std::tanh(g * 2.0f - 1.0f) + 0.8f) * 0.4f;
    const float u = g * 2.0f - 1.0f;
    const float c = (u * u * u + 1.0f) * 0.5f;
    const float s = t * 0.25f + c * 0.75f;
    return std::exp(s * (std::log(2500.0f) - std::log(0.12f)) + std::log(0.12f));
}
} // namespace law

// Speed: -4x .. +4x, the centre at +1x.
inline double SpeedFactor(double normalized) {
    return law::Speed(law::KnobSpeed(static_cast<float>(Clamp01(normalized))));
}
// The pot position for a factor: the inverse of the knob curve, on the straight
// part of the law (inside a snap band it lands on the band's centre value).
inline double SpeedNormalized(double factor) {
    if(!(factor > -4.0))
        return 0.0;
    if(factor >= 4.0)
        return 1.0;
    return factor <= 1.0 ? (factor + 4.0) / 10.0 : 0.5 + (factor - 1.0) / 6.0;
}

// Pitch: -3 .. +2 octaves, the centre at 0.
inline double PitchOctaves(double normalized) {
    return std::log2(static_cast<double>(law::Pitch(law::KnobPitch(static_cast<float>(Clamp01(normalized))))));
}
inline double PitchNormalized(double octaves) {
    if(!(octaves > -3.0))
        return 0.0;
    if(octaves >= 2.0)
        return 1.0;
    const double g = (octaves + 3.0) / 5.0;
    return g <= 0.6 ? g / 1.2 : 0.5 + (g - 0.6) / 0.8;
}

// Density: grains per second, about 0.125 to 983.
inline double DensityRate(double normalized) {
    return law::Density(static_cast<float>(Clamp01(normalized)));
}
inline double DensityNormalized(double rate) {
    if(!(rate > DensityRate(0.0)))
        return 0.0;
    if(rate >= DensityRate(1.0))
        return 1.0;
    double low = 0.0, high = 1.0; // the law rises monotonically
    for(int i = 0; i < 60; ++i) {
        const double middle = 0.5 * (low + high);
        (DensityRate(middle) < rate ? low : high) = middle;
    }
    return 0.5 * (low + high);
}

// Size: the share of the file after Start that the loop takes: the pot squared.
inline double SizeShare(double normalized) {
    const double p = Clamp01(normalized);
    return p * p;
}
inline double SizeNormalized(double share) {
    return std::sqrt(Clamp01(share));
}

// Window: seven shapes, crossfaded continuously, in the order of the firmware.
constexpr int kWindowCount = 7;
constexpr const char* kWindowNames[kWindowCount] = {
    "Gaussian", "Blackman-Harris", "Ramp Down", "Bartlett", "Ramp Up", "Sinc", "Rectangle"};
constexpr const char* kWindowShortNames[kWindowCount] = {"Gauss", "Blackm", "RDown", "Bartl",
                                                         "RUp",   "Sinc",   "Rect"};

// The secondaries as the engine applies them.
inline double SpraySpan(double normalized) {        // +- share of the file length
    return SizeShare(normalized);
}
inline double DisplacementSeconds(double normalized) { // longest random delay
    return 10.0 * SizeShare(normalized);
}
inline double PitchRandomOctaves(double normalized) { // +- octaves
    return 2.0 * Clamp01(normalized);
}
inline double SizeRandomSpan(double normalized) {   // +- on the squared Overlap
    return SizeShare(normalized);
}
inline double MaskShare(double normalized) {        // probability that a grain is muted
    return 0.99 * Clamp01(normalized);
}

// The pitch wheel: 14 bits, 8192 in the middle, +-2 semitones as the firmware
// computes it: (raw - 8192) / 8192 * 2.
constexpr int kPitchBendCentreRaw = 8192;
constexpr int kPitchBendMaxRaw = 16383;
constexpr double kPitchBendRange = 2.0;

inline int PitchBendRaw(double normalized) {
    return static_cast<int>(std::lround(Clamp01(normalized) * kPitchBendMaxRaw));
}
inline double PitchBendSemitones(double normalized) {
    return (PitchBendRaw(normalized) - kPitchBendCentreRaw) / 8192.0 * kPitchBendRange;
}
inline double PitchBendNormalized(double semitones) {
    if(!(semitones > -kPitchBendRange))
        return 0.0;
    const double raw = kPitchBendCentreRaw + semitones / kPitchBendRange * 8192.0;
    return Clamp01(std::round(raw) / kPitchBendMaxRaw);
}

// Layout of the saved state (processor and controller):
//   Version 1 (0.1.0): int32 version, the normalized values of the saved
//   parameters (kSavedIndex) as doubles, int32 length of the sample's path in
//   bytes (0 for none, at most kMaxPathBytes), the path in UTF-8.
constexpr std::int32_t kStateVersion = 1;
constexpr std::int32_t kMaxPathBytes = 32768;
// The controller's own state: version, the editor zoom as a double, the keyboard
// as one byte (1 for shown), its transposition in semitones as int32.
constexpr std::int32_t kControllerStateVersion = 1;

// Messages between the two halves of the plug-in.
// Controller -> processor: a key of the screen keyboard. Velocity 1..127 is a
// strike, 0 the release of the key.
constexpr const char* kNoteMessageId = "TaureonMessierNote";
constexpr const char* kNoteMessageNote = "note";
constexpr const char* kNoteMessageVelocity = "velocity";
// Controller -> processor: load this file (attribute "path", UTF-8 bytes).
constexpr const char* kLoadMessageId = "TaureonMessierLoad";
// Controller -> processor: send the file status. Processor -> controller: the
// status, see FileStatus in file_loader.h.
constexpr const char* kStatusRequestId = "TaureonMessierStatusRequest";
constexpr const char* kStatusMessageId = "TaureonMessierStatus";
constexpr const char* kAttrPath = "path";
constexpr const char* kAttrDetail = "detail";
constexpr const char* kAttrInfo = "info";
constexpr const char* kAttrRequest = "request";
constexpr const char* kAttrState = "state";
constexpr const char* kAttrSerial = "serial";

} // namespace messier
