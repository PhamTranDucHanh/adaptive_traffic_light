#ifndef COMMON_ABSOLUTE_PERIODIC_H
#define COMMON_ABSOLUTE_PERIODIC_H

#include <cerrno>
#include <cstdint>
#include <ctime>

namespace common {

inline bool monotonicNow(timespec& value) noexcept {
  return clock_gettime(CLOCK_MONOTONIC, &value) == 0;
}

inline std::uint64_t monotonicNanoseconds() noexcept {
  timespec now{};
  if (!monotonicNow(now)) {
    return 0U;
  }
  return static_cast<std::uint64_t>(now.tv_sec) * 1000000000ULL +
         static_cast<std::uint64_t>(now.tv_nsec);
}

inline void addMilliseconds(timespec& value,
                            const std::uint32_t milliseconds) noexcept {
  constexpr long kNanosecondsPerSecond = 1000000000L;
  const std::uint64_t additionalNanoseconds =
      static_cast<std::uint64_t>(milliseconds) * 1000000ULL;
  value.tv_sec += static_cast<time_t>(additionalNanoseconds /
                                     kNanosecondsPerSecond);
  value.tv_nsec += static_cast<long>(additionalNanoseconds %
                                    kNanosecondsPerSecond);
  if (value.tv_nsec >= kNanosecondsPerSecond) {
    ++value.tv_sec;
    value.tv_nsec -= kNanosecondsPerSecond;
  }
}

inline bool isAfter(const timespec& left, const timespec& right) noexcept {
  return left.tv_sec > right.tv_sec ||
         (left.tv_sec == right.tv_sec && left.tv_nsec > right.tv_nsec);
}

// clock_nanosleep() returns an error number directly (not -1/errno). With an
// absolute deadline, the same target can safely be retried after EINTR.
template <typename StopPredicate>
int sleepUntil(const timespec& absoluteRelease,
               StopPredicate stopRequested) noexcept {
  int result{0};
  do {
    result = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME,
                             &absoluteRelease, nullptr);
  } while (result == EINTR && !stopRequested());
  return result;
}

// Move a late release forward by complete periods. This preserves the phase
// of the original monotonic schedule and avoids a catch-up busy loop.
inline std::uint32_t advancePastNow(timespec& release,
                                    const std::uint32_t periodMs,
                                    const timespec& now) noexcept {
  std::uint32_t skipped{0U};
  while (!isAfter(release, now)) {
    addMilliseconds(release, periodMs);
    ++skipped;
  }
  return skipped;
}

}  // namespace common

#endif  // COMMON_ABSOLUTE_PERIODIC_H
