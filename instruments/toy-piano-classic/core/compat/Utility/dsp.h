// Desktop stand-in for DaisySP's Utility/dsp.h, so that the firmware sources in
// core/ compile unchanged. It provides the one function they use.
//
// mtof is taken from DaisySP (Source/Utility/dsp.h), MIT License,
// Copyright (c) 2020 Electrosmith, Corp.
#pragma once
#include <cmath>

namespace daisysp {
inline float mtof(float m) {
    return powf(2, (m - 69.0f) / 12.0f) * 440.0f;
}
}
