// The saved state of the sound, read and written the same way by the
// processor and the controller.
#pragma once

#include "parameters.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"

#include <array>
#include <cmath>

namespace torus_classic {

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

// A state of version 1 (0.1.x) in today's terms: Damping mirrored, the filter
// open, free tuning on exactly if the saved Tune is no whole semitone, and
// four voices.
inline SavedValues FromVersion1(const std::array<double, kStateV1Values>& old) {
    SavedValues values{};
    for(int i = 0; i < kStateV1Values; ++i)
        values[i] = old[i];
    values[Index(kParamDamping)] = 1.0 - old[Index(kParamDamping)];
    const double semitones = TuneSemitones(old[Index(kParamTune)]);
    values[kSavedFilter] = kDefaultFilter;
    values[kSavedTuneFree] = std::fabs(semitones - std::round(semitones)) > 1e-9 ? 1.0 : 0.0;
    values[kSavedPolyphony] = kDefaultPolyphony;
    return values;
}

// Reads and checks a whole state. False, with `values` untouched, for an
// unknown version, a state cut short, or a value that is not a finite number
// from 0 to 1. The model, the tuning switch and the number of voices come
// back as the value of the step they fall on.
inline bool ReadSavedValues(Steinberg::IBStream* state, SavedValues& values) {
    Steinberg::IBStreamer stream(state, kLittleEndian);
    Steinberg::int32 version = 0;
    if(!stream.readInt32(version) || version < kOldestStateVersion || version > kStateVersion)
        return false;
    const auto read_value = [&stream](double& value) {
        return stream.readDouble(value) && std::isfinite(value) && value >= 0.0 && value <= 1.0;
    };
    SavedValues read{};
    if(version == 1) {
        std::array<double, kStateV1Values> old{};
        for(double& value : old)
            if(!read_value(value))
                return false;
        read = FromVersion1(old);
    } else {
        // Version 2 ends before the number of voices: four, as it was then.
        const int count = version == 2 ? kStateV2Values : kSavedParamCount;
        read[kSavedPolyphony] = kDefaultPolyphony;
        for(int i = 0; i < count; ++i)
            if(!read_value(read[static_cast<std::size_t>(i)]))
                return false;
    }
    double& model = read[Index(kParamModel)];
    model = ModelNormalized(ModelIndex(model));
    read[kSavedTuneFree] = IsOn(read[kSavedTuneFree]) ? 1.0 : 0.0;
    read[kSavedPolyphony] = PolyphonyNormalized(PolyphonyIndex(read[kSavedPolyphony]));
    values = read;
    return true;
}

static_assert(kSavedIndex[kSavedFilter] == Index(kParamFilter)
              && kSavedIndex[kSavedTuneFree] == Index(kParamTuneFree)
              && kSavedIndex[kSavedPolyphony] == Index(kParamPolyphony));
static_assert(kStateV2Values == kSavedPolyphony && kStateV1Values == kSavedFilter);

} // namespace torus_classic
