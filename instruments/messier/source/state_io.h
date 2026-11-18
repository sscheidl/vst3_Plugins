// The saved state of the sound, read and written the same way by the processor and
// the controller: the parameters and the path of the sample. The running sound
// (position, grains, vocoder phases) is not part of it.
#pragma once

#include "parameters.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"

#include <array>
#include <cmath>
#include <string>

namespace messier {

struct SavedState {
    std::array<double, kSavedParamCount> values{}; // in the order of kSavedIndex
    std::string path;                              // UTF-8, empty for none
};

inline bool WriteSavedState(Steinberg::IBStream* state, const SavedState& saved) {
    Steinberg::IBStreamer stream(state, kLittleEndian);
    if(saved.path.size() > static_cast<std::size_t>(kMaxPathBytes))
        return false;
    if(!stream.writeInt32(kStateVersion))
        return false;
    for(const double value : saved.values)
        if(!stream.writeDouble(value))
            return false;
    const auto length = static_cast<Steinberg::int32>(saved.path.size());
    if(!stream.writeInt32(length))
        return false;
    return length == 0
           || stream.writeRaw(saved.path.data(), length) == static_cast<Steinberg::TSize>(length);
}

// Well-formed UTF-8 without NUL, overlong forms or surrogates.
inline bool IsValidUtf8(const std::string& text) {
    const auto* p = reinterpret_cast<const unsigned char*>(text.data());
    const std::size_t n = text.size();
    for(std::size_t i = 0; i < n;) {
        const unsigned c = p[i];
        if(c == 0)
            return false;
        if(c < 0x80) {
            ++i;
            continue;
        }
        std::size_t length;
        unsigned code;
        if(c >= 0xC2 && c <= 0xDF) {
            length = 2;
            code = c & 0x1F;
        } else if(c >= 0xE0 && c <= 0xEF) {
            length = 3;
            code = c & 0x0F;
        } else if(c >= 0xF0 && c <= 0xF4) {
            length = 4;
            code = c & 0x07;
        } else {
            return false;
        }
        if(i + length > n)
            return false;
        for(std::size_t k = 1; k < length; ++k) {
            if((p[i + k] & 0xC0) != 0x80)
                return false;
            code = code << 6 | (p[i + k] & 0x3F);
        }
        if((length == 3 && (code < 0x800 || (code >= 0xD800 && code <= 0xDFFF)))
           || (length == 4 && (code < 0x10000 || code > 0x10FFFF)))
            return false;
        i += length;
    }
    return true;
}

// Reads and checks a whole state. False, with `saved` untouched, for an unknown
// version, a state cut short, a value that is not a finite number from 0 to 1, or
// a path that is too long or not UTF-8. Freeze comes back as 0 or 1.
inline bool ReadSavedState(Steinberg::IBStream* state, SavedState& saved) {
    Steinberg::IBStreamer stream(state, kLittleEndian);
    Steinberg::int32 version = 0;
    if(!stream.readInt32(version) || (version != kStateVersion && version != 1))
        return false;
    SavedState read;
    const int count = version == 1 ? kSavedParamCountV1 : kSavedParamCount;
    for(int i = 0; i < count; ++i) {
        double& value = read.values[static_cast<std::size_t>(i)];
        if(!stream.readDouble(value) || !std::isfinite(value) || value < 0.0 || value > 1.0)
            return false;
    }
    Steinberg::int32 length = -1;
    if(!stream.readInt32(length) || length < 0 || length > kMaxPathBytes)
        return false;
    read.path.resize(static_cast<std::size_t>(length));
    if(length > 0
       && stream.readRaw(read.path.data(), length) != static_cast<Steinberg::TSize>(length))
        return false;
    if(!IsValidUtf8(read.path))
        return false;
    for(int i = 0; i < kSavedParamCount; ++i)
        if(kSavedIndex[static_cast<std::size_t>(i)] == Index(kParamFreeze)
           || kSavedIndex[static_cast<std::size_t>(i)] == Index(kParamBypass))
            read.values[static_cast<std::size_t>(i)] = IsOn(read.values[static_cast<std::size_t>(i)]) ? 1.0 : 0.0;
    saved = std::move(read);
    return true;
}

} // namespace messier
