// The saved state of the sound, read and written the same way by the
// processor and the controller. After Torus Classic 0.2.0.
#pragma once

#include "parameters.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"

#include <array>
#include <cmath>

namespace elements_synth {

// The values of the saved parameters, in the order of kSavedIndex.
using SavedValues = std::array<double, kSavedParamCount>;

inline bool WriteSavedValues(Steinberg::IBStream* state, const SavedValues& values) {
    Steinberg::IBStreamer stream(state, kLittleEndian);
    if(!stream.writeInt32(kStateVersion))
        return false;
    for(const double value : values)
        if(!stream.writeDouble(value))
            return false;
    return true;
}

// Reads and checks a whole state. False, with `values` untouched, for an
// unknown version, a state cut short, or a value that is not a finite number
// from 0 to 1. Coarse and the Freeze switch come back as the value of the
// step they fall on.
inline bool ReadSavedValues(Steinberg::IBStream* state, SavedValues& values) {
    Steinberg::IBStreamer stream(state, kLittleEndian);
    Steinberg::int32 version = 0;
    if(!stream.readInt32(version) || version != kStateVersion)
        return false;
    SavedValues read{};
    for(double& value : read)
        if(!stream.readDouble(value) || !std::isfinite(value) || value < 0.0 || value > 1.0)
            return false;
    double& coarse = read[static_cast<std::size_t>(Index(kParamCoarse))];
    coarse = CoarseNormalized(CoarseSemitones(coarse));
    double& freeze = read[static_cast<std::size_t>(Index(kParamFreeze))];
    freeze = IsOn(freeze) ? 1.0 : 0.0;
    values = read;
    return true;
}

} // namespace elements_synth
