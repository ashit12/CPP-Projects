#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <utility>
namespace lob {
template <typename T, size_t Capacity> class SPSCQueue {
  static_assert((Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of 2!");

public:
  bool push(const T &item) {
    size_t t = tail.load(std::memory_order::relaxed);
    if (t - head.load(std::memory_order_acquire) == Capacity) {
      return false;
    }
    buffer[t & (Capacity - 1)] = item;
    tail.store(t + 1, std::memory_order::release);
    return true;
  }
  std::optional<T> pop() {
    size_t h = head.load(std::memory_order::acquire);
    if (h == tail.load(std::memory_order_relaxed)) {
      return std::nullopt;
    }
    T item = std::move(buffer[h & (Capacity - 1)]);
    head.store(h + 1, std::memory_order::release);
    return item;
  }

private:
  alignas(64) std::array<T, Capacity> buffer{};
  alignas(64) std::atomic<size_t> head{0};
  alignas(64) std::atomic<size_t> tail{0};
};
} // namespace lob