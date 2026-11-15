// Hands the newest of a series of values from one thread to another without
// locks or waiting on either side: the writer always finds a free slot, the
// reader always gets a complete value. One writer and one reader at a time.
#pragma once

#include <atomic>
#include <cstdint>

namespace torus_classic {

template <typename T>
class LatestValue {
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "needs lock-free atomics");

  public:
    // Writer side. A value published before the last one was taken replaces it.
    void Publish(const T& value) {
        slots_[write_] = value;
        write_ = shared_.exchange(write_ | kFresh, std::memory_order_acq_rel) & kIndex;
    }

    // Reader side. True and the newest value if one was published since the
    // last call.
    bool Take(T& value) {
        if(!(shared_.load(std::memory_order_acquire) & kFresh))
            return false;
        read_ = shared_.exchange(read_, std::memory_order_acq_rel) & kIndex;
        value = slots_[read_];
        return true;
    }

  private:
    static constexpr std::uint32_t kIndex = 3, kFresh = 4;
    T slots_[3]{};
    std::atomic<std::uint32_t> shared_{1};
    std::uint32_t write_ = 0; // owned by the writer
    std::uint32_t read_ = 2;  // owned by the reader
};

} // namespace torus_classic
