#ifndef TRAFFIC_TIMING_DECISION_COMMON_H
#define TRAFFIC_TIMING_DECISION_COMMON_H

#include <atomic>
#include <cstdint>
#include <ctime>

namespace common {

bool monotonicNow(timespec& value) noexcept;
std::uint64_t monotonicNanoseconds() noexcept;
void addMilliseconds(timespec& value, std::uint32_t milliseconds) noexcept;
std::uint32_t advancePastNow(timespec& release, std::uint32_t periodMs,
                             const timespec& now) noexcept;

// Waits until an absolute CLOCK_MONOTONIC release time. A stop request is
// observed before and after the current sleep; clock_nanosleep itself remains
// intentionally non-interruptible by an application wake channel.
class PeriodicWait final {
 public:
  PeriodicWait() noexcept = default;
  ~PeriodicWait() = default;

  PeriodicWait(const PeriodicWait&) = delete;
  PeriodicWait& operator=(const PeriodicWait&) = delete;

  std::int32_t waitUntil(const timespec& absoluteRelease) noexcept;
  void requestStop() noexcept;

 private:
  std::atomic_bool stopRequested_{false};
};

}  // namespace common

#endif  // TRAFFIC_TIMING_DECISION_COMMON_H
