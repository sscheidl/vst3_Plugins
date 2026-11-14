// Output stage. Adapted from Daisy-Seed
// instruments/physical-modeling/src/audio_output.h: Output() is unchanged, the
// dry/wet Mix() is gone with the reverb. See ../UPSTREAM.md.
#pragma once
#include "config.h"
#include <algorithm>
#include <cmath>
// The guard against non-finite samples below needs IEEE semantics. The VST3 SDK's
// build files select /fp:fast for everything; this target overrides it.
#if defined(_MSC_VER) && defined(_M_FP_FAST)
#error "Torus Classic's engine must not be compiled with /fp:fast"
#endif
namespace torus_classic {
inline float Output(float signal, float master) {
    if(!std::isfinite(signal)) return 0;
    const float x = signal * (0.8f * Unit(master));
    const float magnitude = std::fabs(x);
    // Linear through 0.6 FS, smooth knee asymptotic to 0.8; final roundoff guard.
    if(magnitude <= 0.6f) return x;
    const float excess = magnitude - 0.6f;
    const float limited = std::min(0.799999f, 0.6f + 0.2f * (excess / (0.2f + excess)));
    return std::copysign(limited, x);
}
}
