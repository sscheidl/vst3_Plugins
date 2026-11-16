// The saved state of the plug-in: what the processor writes and what both the
// processor and the controller read.
//
// Layout, little-endian: the version (int32); master pitch, attack, decay,
// volume and tuning as normalized doubles; run and keys-transpose as one byte
// each (0 or 1); then the sequencer: tonic, scale mode, algorithm, mutation,
// note probability, octave, length and clock divide as one byte each; from
// version 2 the mutation lock (0 or 1) and mutate-every (0 to 16) as one byte
// each; the 16 steps as signed bytes, and the state of its random generator
// (int64). How many passes have gone by is not saved, as the playhead isn't.
//
// Version 1 (plug-in 0.1.0) had seven scale modes in another order, no lock
// and no mutate-every; it is read with its modes moved to their new numbers,
// the lock open and mutation on every tick.
#pragma once

#include "generative_sequencer.h"
#include "parameters.h"

#include "base/source/fstreamer.h"

#include <cmath>
#include <cstdint>

namespace taureon::toy_piano_generative {

constexpr std::int32_t kStateVersion = 2;
// Bytes of a saved state, of this version and of version 1.
constexpr int kStateSize = 4 + 5 * 8 + 2 + 8 + 2 + generative::kNumSteps + 8;
constexpr int kStateSizeVersion1 = kStateSize - 2;

// The scale modes of version 1, by their number there: major, minor,
// pentatonic, odd, octave, fifths, tonic.
constexpr std::uint8_t kScaleModesOfVersion1[7] = {0, 1, 9, generative::kOddMode, 14, 15,
                                                   generative::kTonicOnlyMode};

struct SavedState {
    double master_pitch = kDefaultMasterPitch;
    double attack = kDefaultAttack;
    double decay = kDefaultDecay;
    double volume = kDefaultVolume;
    double tuning = kDefaultTuning;
    bool run = true;
    bool keys_transpose = true;
    // Where the playhead stands is not saved: a loaded pattern starts at its
    // first step.
    generative::State sequencer;
};

inline bool WriteState(Steinberg::IBStreamer& stream, const SavedState& state) {
    const generative::State& s = state.sequencer;
    bool ok = stream.writeInt32(kStateVersion) && stream.writeDouble(state.master_pitch)
              && stream.writeDouble(state.attack) && stream.writeDouble(state.decay)
              && stream.writeDouble(state.volume) && stream.writeDouble(state.tuning)
              && stream.writeInt8(state.run ? 1 : 0)
              && stream.writeInt8(state.keys_transpose ? 1 : 0);
    for(const std::uint8_t value : {s.tonic, s.scale_mode, s.algorithm, s.mutation,
                                    s.note_probability, s.octave, s.length, s.clock_divide,
                                    s.mutation_lock, s.mutate_every})
        ok = ok && stream.writeInt8u(value);
    for(const std::int8_t step : s.steps)
        ok = ok && stream.writeInt8(step);
    return ok && stream.writeInt64u(s.random);
}

// Reads and checks a whole state. False for anything cut short, of another
// version or out of range; `state` is then left untouched.
inline bool ReadState(Steinberg::IBStreamer& stream, SavedState& state) {
    using Steinberg::int8;
    Steinberg::int32 version = 0;
    SavedState read;
    int8 run = 0, keys_transpose = 0;
    if(!stream.readInt32(version) || version < 1 || version > kStateVersion
       || !stream.readDouble(read.master_pitch) || !stream.readDouble(read.attack)
       || !stream.readDouble(read.decay) || !stream.readDouble(read.volume)
       || !stream.readDouble(read.tuning) || !stream.readInt8(run)
       || !stream.readInt8(keys_transpose))
        return false;
    generative::State& s = read.sequencer;
    for(std::uint8_t* value : {&s.tonic, &s.scale_mode, &s.algorithm, &s.mutation,
                               &s.note_probability, &s.octave, &s.length, &s.clock_divide})
        if(!stream.readInt8u(*value))
            return false;
    if(version >= 2 && (!stream.readInt8u(s.mutation_lock) || !stream.readInt8u(s.mutate_every)))
        return false;
    if(version == 1) {
        if(s.scale_mode >= 7)
            return false;
        s.scale_mode = kScaleModesOfVersion1[s.scale_mode];
    }
    for(std::int8_t& step : s.steps) {
        int8 value = 0;
        if(!stream.readInt8(value))
            return false;
        step = value;
    }
    Steinberg::uint64 random = 0;
    if(!stream.readInt64u(random))
        return false;
    s.random = random;
    for(const double value :
        {read.master_pitch, read.attack, read.decay, read.volume, read.tuning})
        if(!(value >= 0.0 && value <= 1.0)) // also refuses NaN
            return false;
    if((run != 0 && run != 1) || (keys_transpose != 0 && keys_transpose != 1)
       || !generative::IsValid(s))
        return false;
    read.run = run != 0;
    read.keys_transpose = keys_transpose != 0;
    state = read;
    return true;
}

} // namespace taureon::toy_piano_generative
