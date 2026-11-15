// Taken from Torus Classic 0.2.0; only the namespace and the wording differ.
//
// The normalized values of the parameters, shared between the audio
// thread and everybody else, so that both always see a complete state.
//
// Two things write parameter values: a loaded state (setState, from the
// host's UI thread or a worker) and automation (process, audio thread). A
// loaded state replaces everything at once; automation changes one value at
// a time afterwards. The difficulty is their order: the audio thread applies
// a loaded state only at its next block, and automation of a block that was
// already running must not leak into the newer state.
//
// So every loaded state gets a generation number. The audio thread works on
// the generation it has adopted and tags each automation value with it. The
// current state is the last loaded one, overlaid with those automation values
// that carry its generation: values of an older generation are history.
//
// The audio side never locks or waits. The other side takes a mutex and may
// retry a read. Free of VST3 types.
#pragma once

#include "latest_value.h"
#include "parameters.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

namespace elements_synth {

class ParameterState {
  public:
    using Clock = std::chrono::steady_clock;
    using Values = std::array<double, kParamCount>;

    // A loaded state as the audio thread receives it: complete in itself.
    struct Loaded {
        Values values{};
        std::uint64_t generation = 0;
        Clock::time_point loaded_at{};
    };

    ParameterState() {
        for(int i = 0; i < kParamCount; ++i)
            base_[i] = kDefaults[i];
    }

    // --- Any thread but the audio thread -----------------------------------

    // A state was loaded: the saved values (in the order of kSavedIndex),
    // the pitch wheel back in its centre. Becomes the current state at once
    // and waits for the audio thread to take it.
    void Load(const std::array<double, kSavedParamCount>& saved) {
        const std::lock_guard<std::mutex> lock(mutex_);
        for(int i = 0; i < kSavedParamCount; ++i)
            base_[static_cast<std::size_t>(kSavedIndex[static_cast<std::size_t>(i)])] =
                saved[static_cast<std::size_t>(i)];
        base_[kParamPitchBend - kFirstParam] = kDefaultPitchBend;
        ++generation_;
        Loaded loaded;
        loaded.values = base_;
        loaded.generation = generation_;
        loaded.loaded_at = Clock::now();
        loaded_.Publish(loaded);
    }

    // The current state: what getState() saves and what a restart begins with.
    Values Current() const {
        const std::lock_guard<std::mutex> lock(mutex_);
        Values values = base_;
        for(int i = 0; i < kParamCount; ++i) {
            const Written written = slots_[i].Read();
            if(written.generation == generation_)
                values[i] = written.value;
        }
        return values;
    }

    // --- Audio thread (or a lifecycle call while no audio runs) -------------

    // True and the newest loaded state if one arrived since the last call.
    bool TakeLoaded(Loaded& loaded) { return loaded_.Take(loaded); }

    // The audio side now renders this loaded state; values written from here
    // on belong to it.
    void Adopt(std::uint64_t generation) { adopted_ = generation; }

    // An automation value, for the state adopted last.
    void Write(int index, double value) {
        if(index >= 0 && index < kParamCount)
            slots_[index].Write({adopted_, value});
    }

  private:
    struct Written {
        std::uint64_t generation;
        double value;
    };

    // One value with its generation, written by one thread without waiting,
    // read by others as a matching pair (a sequence lock: the reader retries
    // while a write is under way).
    class Slot {
      public:
        void Write(const Written& written) {
            const std::uint32_t sequence = sequence_.load(std::memory_order_relaxed);
            sequence_.store(sequence + 1, std::memory_order_release); // odd: writing
            generation_.store(written.generation, std::memory_order_release);
            value_.store(written.value, std::memory_order_release);
            sequence_.store(sequence + 2, std::memory_order_release);
        }
        Written Read() const {
            for(;;) {
                const std::uint32_t before = sequence_.load(std::memory_order_acquire);
                if(before & 1u)
                    continue;
                const Written written{generation_.load(std::memory_order_acquire),
                                      value_.load(std::memory_order_acquire)};
                if(sequence_.load(std::memory_order_acquire) == before)
                    return written;
            }
        }

      private:
        std::atomic<std::uint32_t> sequence_{0};
        // No loaded state has this generation: nothing written yet.
        std::atomic<std::uint64_t> generation_{~std::uint64_t{0}};
        std::atomic<double> value_{0.0};
    };

    mutable std::mutex mutex_;       // never taken by the audio thread
    Values base_{};                  // the last loaded state, or the defaults
    std::uint64_t generation_ = 0;   // of base_
    LatestValue<Loaded> loaded_;
    std::array<Slot, kParamCount> slots_;
    std::uint64_t adopted_ = 0;      // owned by the audio side
};

} // namespace elements_synth
