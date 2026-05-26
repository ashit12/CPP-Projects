#pragma once
#include <atomic>
#include <optional>
#include <array>

// Bounded Chase-Lev work-stealing deque.
//
// One thread (the "owner") may call push() and pop(); any number of
// other threads may call steal() concurrently. The owner operates at
// the "bottom" of the deque (LIFO, best cache locality) while thieves
// take from the "top" (FIFO, minimal contention with the owner).
//
// Invariants:
//   - top_ never decreases.
//   - bottom_ is only ever decremented by the owner inside pop() and
//     is restored before pop() returns, so any observer eventually
//     sees a consistent size = bottom_ - top_ >= 0.
//   - push() returns false if size would exceed Capacity; callers
//     are expected to handle back-pressure (e.g. execute inline).
//
// The class is alignas(64) and its hot atomics each get their own
// cache line to avoid false sharing between owner and thieves.
template <typename T, size_t Capacity>
class alignas(64) WorkerDeque
{

    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                  "WorkerDeque Capacity MUST be a power of 2 (e.g., 1024, 2048)!");

public:
    WorkerDeque() : top_(0), bottom_(0) {}

    // Owner-only. Loads are relaxed because the owner has a coherent
    // view of its own updates; the capacity check is allowed to be
    // conservative (a stale top_ only causes a spurious failure, never
    // a buffer overflow).
    bool push(T item)
    {
        int64_t b = bottom_.load(std::memory_order_relaxed);
        if (b - top_.load(std::memory_order_relaxed) >= static_cast<int64_t>(Capacity))
        {
            return false;
        }
        buffer_[getMask(b)].store(item, std::memory_order_relaxed);
        // Release publishes the slot write to thieves that acquire bottom_.
        bottom_.store(b + 1, std::memory_order_release);
        return true;
    }

    // Owner-only. Three cases after speculatively decrementing bottom_:
    //   sz <  0: deque empty, restore bottom_ and return nullopt.
    //   sz >  0: at least one element behind any thief, take it locally.
    //   sz == 0: race with a thief for the last element; resolve via CAS.
    //
    // The bottom_ store and top_ load both use seq_cst to provide the
    // StoreLoad ordering that steal() relies on: its seq_cst fence
    // between top_ and bottom_ loads sits in the same total order, so
    // owner and thief cannot both believe the deque holds an element
    // when only one slot remains.
    std::optional<T> pop()
    {
        int64_t b = bottom_.load(std::memory_order_relaxed) - 1;
        bottom_.store(b, std::memory_order_seq_cst);
        int64_t t = top_.load(std::memory_order_seq_cst);
        int64_t sz = b - t;
        if (sz < 0)
        {
            // Release so a thief that observes this restored bottom_
            // via the acquire load in steal() still synchronizes-with
            // the most recent push() and sees the slot writes.
            bottom_.store(t, std::memory_order_release);
            return std::nullopt;
        }
        if (sz > 0)
        {
            // Slot was written by us and no thief can reach it
            // (their range ends at top_), so relaxed is sufficient.
            return buffer_[getMask(b)].load(std::memory_order_relaxed);
        }
        // sz == 0: race a thief for the last task. compare_exchange
        // takes its expected by reference and overwrites it on
        // failure, so we pass a scratch copy and keep our own t
        // intact to use when restoring bottom_ below.
        int64_t expected = t;
        bool won = top_.compare_exchange_strong(expected, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed);
        // Whether we won or lost, exactly one of {owner, thief}
        // consumed slot t. The deque is now empty, so make that the
        // observable state by setting bottom_ = top_ = t + 1.
        // Release pairs with steal()'s acquire load on bottom_ so
        // thieves that observe this restored value still have a
        // happens-before edge back to the most recent push().
        bottom_.store(t + 1, std::memory_order_release);
        if (won)
        {
            return buffer_[getMask(b)].load(std::memory_order_relaxed);
        }
        return std::nullopt;
    }

    // Called by non-owner threads. The seq_cst fence between the two
    // acquire loads is the critical correctness piece: without it, the
    // CPU is allowed to reorder the bottom_ load before the top_ load,
    // and on weak memory architectures a thief could observe a stale
    // bottom_ while the owner is mid-pop() and end up double-claiming
    // the same slot.
    std::optional<T> steal()
    {
        int64_t t = top_.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        int64_t b = bottom_.load(std::memory_order_acquire);
        int64_t sz = b - t;
        if (sz <= 0)
        {
            return std::nullopt;
        }
        // Acquire pairs with push()'s release on bottom_, so the slot
        // write happens-before this read.
        T task = buffer_[getMask(t)].load(std::memory_order_acquire);
        if (top_.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
        {
            return task;
        }
        return std::nullopt;
    }

private:
    // Each atomic on its own cache line: owner writes bottom_ on every
    // push/pop, thieves CAS top_ on every successful steal. Colocating
    // them would generate constant cross-core invalidations.
    alignas(64) std::atomic<int64_t> top_;
    alignas(64) std::atomic<int64_t> bottom_;
    std::array<std::atomic<T>, Capacity> buffer_;
    inline int64_t getMask(int64_t val)
    {
        return val & (Capacity - 1);
    }
};
