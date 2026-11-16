// Hands the sequencer's state from one thread to another without a lock.
// Free of VST3 types.
#pragma once

#include "generative_sequencer.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace taureon::toy_piano_generative {

// A few 64-bit words that one thread writes and others read. The writer never
// waits. A reader that meets a write in progress tries again; it never sees
// half of one. (A sequence lock; the words themselves are atomics, so that no
// read races with a write.)
template <std::size_t N>
class SharedWords {
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "needs lock-free atomics");

  public:
    using Words = std::array<std::uint64_t, N>;

    // One writer at a time.
    void Write(const Words& words) {
        const std::uint32_t sequence = sequence_.load();
        sequence_.store(sequence + 1); // odd: a write is in progress
        for(std::size_t i = 0; i < N; ++i)
            words_[i].store(words[i]);
        sequence_.store(sequence + 2);
    }

    // False if a write got in the way on every attempt.
    bool TryRead(Words& words, int attempts) const {
        for(int attempt = 0; attempt < attempts; ++attempt) {
            const std::uint32_t before = sequence_.load();
            if(before & 1u)
                continue;
            for(std::size_t i = 0; i < N; ++i)
                words[i] = words_[i].load();
            if(sequence_.load() == before)
                return true;
        }
        return false;
    }

    // For threads that may wait: a write takes a few nanoseconds.
    Words Read() const {
        Words words{};
        while(!TryRead(words, 64)) {}
        return words;
    }

  private:
    std::atomic<std::uint32_t> sequence_{0};
    std::array<std::atomic<std::uint64_t>, N> words_{};
};

// The sequencer's state and the number of the load request it stems from.
constexpr std::size_t kStateWords = 5;
using StateWords = SharedWords<kStateWords>::Words;

inline StateWords PackState(const generative::State& state, std::uint32_t generation) {
    StateWords words{};
    std::memcpy(&words[0], state.steps.data(), 16);
    const std::uint8_t settings[8] = {state.tonic,            state.scale_mode, state.algorithm,
                                      state.mutation,         state.note_probability,
                                      state.octave,           state.length,
                                      state.clock_divide};
    std::memcpy(&words[2], settings, 8);
    words[3] = static_cast<std::uint64_t>(state.position)
               | static_cast<std::uint64_t>(state.divide_count) << 8
               | static_cast<std::uint64_t>(state.loop_on) << 16
               | static_cast<std::uint64_t>(state.at_start) << 24
               | static_cast<std::uint64_t>(generation) << 32;
    words[4] = state.random;
    return words;
}

inline std::uint32_t GenerationOf(const StateWords& words) {
    return static_cast<std::uint32_t>(words[3] >> 32);
}

inline generative::State UnpackState(const StateWords& words) {
    generative::State state;
    std::memcpy(state.steps.data(), &words[0], 16);
    std::uint8_t settings[8];
    std::memcpy(settings, &words[2], 8);
    state.tonic = settings[0];
    state.scale_mode = settings[1];
    state.algorithm = settings[2];
    state.mutation = settings[3];
    state.note_probability = settings[4];
    state.octave = settings[5];
    state.length = settings[6];
    state.clock_divide = settings[7];
    state.position = static_cast<std::uint8_t>(words[3]);
    state.divide_count = static_cast<std::uint8_t>(words[3] >> 8);
    state.loop_on = static_cast<std::uint8_t>(words[3] >> 16);
    state.at_start = static_cast<std::uint8_t>(words[3] >> 24);
    state.random = words[4];
    return state;
}

} // namespace taureon::toy_piano_generative
