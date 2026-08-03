#ifndef TRAFFIC_TIMING_DECISION_COMMON_H
#define TRAFFIC_TIMING_DECISION_COMMON_H

#include <pthread.h>

#include <cstdint>
#include <ctime>

namespace common {

bool monotonicNow(timespec& value) noexcept;
std::uint64_t monotonicNanoseconds() noexcept;
void addMilliseconds(timespec& value, std::uint32_t milliseconds) noexcept;
std::uint32_t advancePastNow(timespec& release, std::uint32_t periodMs,
                             const timespec& now) noexcept;

// Waits until an absolute CLOCK_MONOTONIC release time. Lifecycle can wake the
// periodic thread immediately by requesting stop through the condition
// variable.
class PeriodicWait final {
 public:
  PeriodicWait() noexcept;
  ~PeriodicWait();

  PeriodicWait(const PeriodicWait&) = delete;
  PeriodicWait& operator=(const PeriodicWait&) = delete;

  bool valid() const noexcept;
  std::int32_t waitUntil(const timespec& absoluteRelease) noexcept;
  void requestStop() noexcept;

 private:
  pthread_mutex_t mutex_{};
  pthread_cond_t condition_{};
  bool mutexReady_{false};
  bool conditionReady_{false};
  bool stopRequested_{false};
};

}  // namespace common

#endif  // TRAFFIC_TIMING_DECISION_COMMON_H
