#pragma once
#include <array>
#include <optional>
#include <atomic>

// Bounded single-producer / multi-consumer ring queue.
//
// Contract:
//   - push(): one producer thread at a time. Concurrent producers are
//             not supported; the lone producer fully owns bottom_.
//   - pop():  any number of concurrent consumers. They race for slots
//             via a CAS on top_.
//   - Capacity must be a power of two so we can mask instead of mod.
//
// Memory model:
//   The producer publishes a slot by writing the payload (relaxed) and
//   then storing the new bottom_ with release semantics. A consumer
//   that observes bottom_ > pos via an acquire load is guaranteed to
//   see the payload write, so the buffer load itself can be relaxed.
template <typename Task, size_t Capacity>
class GlobalSPMCQueue
{
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2!");

public:
    // Returns false if the queue is full. The capacity check uses an
    // acquire load on top_ so the producer observes consumer-advanced
    // top_ values promptly and reclaims slots as soon as possible.
    bool push(Task item)
    {
        int64_t b = bottom_.load(std::memory_order_relaxed);

        if (b - top_.load(std::memory_order_acquire) >= static_cast<int64_t>(Capacity))
        {
            return false;
        }

        buffer_[b & (Capacity - 1)].store(item, std::memory_order_relaxed);
        // Release pairs with the acquire of bottom_ in pop(): publishes
        // the slot write above to any consumer that sees this update.
        bottom_.store(b + 1, std::memory_order_release);
        return true;
    }

    // Returns nullopt if the queue is observed empty. On contention
    // among consumers the losing CAS retries with the updated pos.
    std::optional<Task> pop()
    {
        int64_t pos = top_.load(std::memory_order_relaxed);
        while (true)
        {
            // Acquire on bottom_ pairs with the producer's release, so
            // if we see pos < bottom_ then the slot write is visible.
            if (pos >= bottom_.load(std::memory_order_acquire))
            {
                return std::nullopt;
            }

            Task item = buffer_[pos & (Capacity - 1)].load(std::memory_order_relaxed);
            // On success the CAS claims the slot for this consumer.
            // On failure pos is refreshed and we re-read the slot.
            if (top_.compare_exchange_weak(pos, pos + 1, std::memory_order_release, std::memory_order_relaxed))
            {
                return item;
            }
        }
    }

private:
    // top_ and bottom_ live on different cache lines so producer-side
    // updates of bottom_ never invalidate the consumer-side line that
    // holds top_, and vice versa.
    alignas(64) std::atomic<int64_t> top_{0};
    alignas(64) std::atomic<int64_t> bottom_{0};
    std::array<std::atomic<Task>, Capacity> buffer_;
};
