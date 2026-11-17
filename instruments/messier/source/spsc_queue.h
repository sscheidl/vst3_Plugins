#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// Copied from Daisy-Seed instruments/physical-modeling/src/spsc_queue.h by way
// of Torus Classic 0.2.0; the namespace is changed and SizeForProducer() added.
// Carries screen keyboard notes from the host's UI thread to the audio thread.
namespace messier {
// Lock-free single-producer/single-consumer queue. Exactly one context may call
// Push() and exactly one other (or the same) context may call Pop().
template <typename T, std::size_t N>
class SpscQueue {
    static_assert(N >= 2 && (N & (N - 1)) == 0, "N must be a power of two");
    static_assert(std::atomic<uint32_t>::is_always_lock_free, "needs lock-free atomics");

  public:
    // Returns false (and drops the item) when the queue is full.
    bool Push(const T& item) {
        const uint32_t head = head_.load(std::memory_order_relaxed);
        if(head - tail_.load(std::memory_order_acquire) >= N)
            return false;
        items_[head & (N - 1)] = item;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // The number of items in the queue as the producer sees it: never fewer
    // than there are, since the consumer may have taken some meanwhile.
    // Producer only. Added for Mendelejew 0.1.2.
    std::size_t SizeForProducer() const {
        return head_.load(std::memory_order_relaxed) - tail_.load(std::memory_order_acquire);
    }

    // Returns false when the queue is empty.
    bool Pop(T& item) {
        const uint32_t tail = tail_.load(std::memory_order_relaxed);
        if(tail == head_.load(std::memory_order_acquire))
            return false;
        item = items_[tail & (N - 1)];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

  private:
    std::array<T, N> items_{};
    std::atomic<uint32_t> head_{0};
    std::atomic<uint32_t> tail_{0};
};
}
