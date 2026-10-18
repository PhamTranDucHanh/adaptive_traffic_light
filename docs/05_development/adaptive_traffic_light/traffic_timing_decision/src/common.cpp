#include "common.h"

#include <cerrno>

namespace {

enum class TimeConversion : std::uint64_t {
  kNanosecondsPerMillisecond = 1000000ULL,
  kNanosecondsPerSecond = 1000000000ULL,
};

constexpr std::uint64_t toNanoseconds(const TimeConversion value) noexcept {
  return static_cast<std::uint64_t>(value);
}

bool isAfter(const timespec& left, const timespec& right) noexcept {
  return left.tv_sec > right.tv_sec ||
         (left.tv_sec == right.tv_sec && left.tv_nsec > right.tv_nsec);
}

}  // namespace

namespace common {

bool monotonicNow(timespec& value) noexcept {
  return clock_gettime(CLOCK_MONOTONIC, &value) == std::int32_t{};
}

std::uint64_t monotonicNanoseconds() noexcept {
  timespec now{};
  if (!monotonicNow(now)) {
    return std::uint64_t{};
  }
  return static_cast<std::uint64_t>(now.tv_sec) *
             toNanoseconds(TimeConversion::kNanosecondsPerSecond) +
         static_cast<std::uint64_t>(now.tv_nsec);
}

void addMilliseconds(timespec& value,
                     const std::uint32_t milliseconds) noexcept {
  const std::uint64_t additionalNanoseconds =
      static_cast<std::uint64_t>(milliseconds) *
      toNanoseconds(TimeConversion::kNanosecondsPerMillisecond);
  const std::uint64_t nanosecondsPerSecond =
      toNanoseconds(TimeConversion::kNanosecondsPerSecond);
  value.tv_sec += static_cast<decltype(value.tv_sec)>(additionalNanoseconds /
                                                      nanosecondsPerSecond);
  value.tv_nsec += static_cast<decltype(value.tv_nsec)>(additionalNanoseconds %
                                                        nanosecondsPerSecond);
  if (static_cast<std::uint64_t>(value.tv_nsec) >= nanosecondsPerSecond) {
    ++value.tv_sec;
    value.tv_nsec -= static_cast<decltype(value.tv_nsec)>(nanosecondsPerSecond);
  }
}

std::uint32_t advancePastNow(timespec& release, const std::uint32_t periodMs,
                             const timespec& now) noexcept {
  std::uint32_t skipped{};
  while (!isAfter(release, now)) {
    addMilliseconds(release, periodMs);
    ++skipped;
  }
  return skipped;
}

std::int32_t PeriodicWait::waitUntil(const timespec& absoluteRelease) noexcept {
  if (stopRequested_.load(std::memory_order_acquire)) {
    return ECANCELED;
  }

  std::int32_t result{};
  do {
    result = static_cast<std::int32_t>(
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &absoluteRelease,
                        nullptr));
  } while (result == EINTR &&
           !stopRequested_.load(std::memory_order_acquire));

  return stopRequested_.load(std::memory_order_acquire) ? ECANCELED : result;
}

void PeriodicWait::requestStop() noexcept {
  stopRequested_.store(true, std::memory_order_release);
}

}  // namespace common
