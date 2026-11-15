// Torus Classic engine constants. Adapted from Daisy-Seed
// instruments/physical-modeling/src/config.h; see ../UPSTREAM.md for what was
// removed (hardware key table, reverb buffer size, switch decoding, the
// 48-sample callback size).
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace torus_classic {
// The DSP runs at this rate whatever the host uses; torus/dsp/dsp.h has it
// built into its algorithms.
constexpr float kInternalSampleRate = 48000.0f;
constexpr size_t kQuantum = 12;
constexpr size_t kVoices = 4;
inline float Unit(float x) { return x >= 0.0f ? (x <= 1.0f ? x : 1.0f) : 0.0f; }
inline float Transpose(float x) { return 48.0f * Unit(x) - 24.0f; }
inline float Bend(unsigned x) {
    return x <= 8192 ? (float(x) - 8192.0f) / 4096.0f
                     : (float(x > 16383 ? 16383 : x) - 8192.0f) * (2.0f / 8191.0f);
}
}
