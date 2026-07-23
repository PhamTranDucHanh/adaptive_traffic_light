#ifndef TRAFFIC_SIGNAL_CONTROLLER_COMMON_SPSC_QUEUE_H_
#define TRAFFIC_SIGNAL_CONTROLLER_COMMON_SPSC_QUEUE_H_

#include <array>
#include <atomic>
#include <cstddef>

namespace ctrl::concurrency {

template <typename T, std::size_t Capacity>
class SpscQueue final {
 public:
  static_assert(Capacity >= 2U,
                "SPSC queue capacity must be at least 2");

  bool TryPush(const T& value) noexcept {
    const std::size_t write =
        writeIndex_.load(std::memory_order_relaxed);
    const std::size_t next = Increment(write);

    if (next ==
        readIndex_.load(std::memory_order_acquire)) {
      return false;
    }

    storage_[write] = value;
    writeIndex_.store(next, std::memory_order_release);
    return true;
  }

  bool TryPop(T& value) noexcept {
    const std::size_t read =
        readIndex_.load(std::memory_order_relaxed);

    if (read ==
        writeIndex_.load(std::memory_order_acquire)) {
      return false;
    }

    value = storage_[read];
    readIndex_.store(Increment(read),
                     std::memory_order_release);
    return true;
  }

  bool Empty() const noexcept {
    return readIndex_.load(std::memory_order_acquire) ==
           writeIndex_.load(std::memory_order_acquire);
  }

 private:
  static constexpr std::size_t Increment(
      const std::size_t index) noexcept {
    return (index + 1U) % Capacity;
  }

  std::array<T, Capacity> storage_{};

  alignas(64) std::atomic<std::size_t> writeIndex_{0U};
  alignas(64) std::atomic<std::size_t> readIndex_{0U};
};

}  // namespace ctrl::concurrency

#endif