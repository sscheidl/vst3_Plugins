// The saved state of the sound, read and written the same way by the
// processor and the controller.
#pragma once

#include "parameters.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"

#include <array>
#include <cmath>

namespace torus_classic {

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

// Reads and checks a whole state. False, with `values` untouched, for another
// version, a state cut short, or a value that is not a finite number from 0
// to 1. The model comes back as the value of the step it falls on.
inline bool ReadSavedValues(Steinberg::IBStream* state, SavedValues& values) {
    Steinberg::IBStreamer stream(state, kLittleEndian);
    Steinberg::int32 version = 0;
    if(!stream.readInt32(version) || version != kStateVersion)
        return false;
    SavedValues read{};
    for(double& value : read)
        if(!stream.readDouble(value) || !std::isfinite(value) || value < 0.0 || value > 1.0)
            return false;
    double& model = read[kParamModel - kFirstParam];
    model = ModelNormalized(ModelIndex(model));
    values = read;
    return true;
}

} // namespace torus_classic
