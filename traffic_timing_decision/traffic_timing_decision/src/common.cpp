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

PeriodicWait::PeriodicWait() noexcept {
  if (pthread_mutex_init(&mutex_, nullptr) != std::int32_t{}) {
    return;
  }
  mutexReady_ = true;

  pthread_condattr_t attributes{};
  if (pthread_condattr_init(&attributes) != std::int32_t{}) {
    return;
  }
  const std::int32_t clockResult =
      pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
  if (clockResult == std::int32_t{} &&
      pthread_cond_init(&condition_, &attributes) == std::int32_t{}) {
    conditionReady_ = true;
  }
  (void)pthread_condattr_destroy(&attributes);
}

PeriodicWait::~PeriodicWait() {
  if (conditionReady_) {
    (void)pthread_cond_destroy(&condition_);
  }
  if (mutexReady_) {
    (void)pthread_mutex_destroy(&mutex_);
  }
}

bool PeriodicWait::valid() const noexcept {
  return mutexReady_ && conditionReady_;
}

std::int32_t PeriodicWait::waitUntil(const timespec& absoluteRelease) noexcept {
  if (!valid()) {
    return EINVAL;
  }

  timespec now{};
  if (!monotonicNow(now)) {
    return errno != std::int32_t{} ? errno : EINVAL;
  }

  if (isAfter(absoluteRelease, now)) {
    std::int32_t result = pthread_mutex_lock(&mutex_);
    if (result != std::int32_t{}) {
      return result;
    }

    while (!stopRequested_) {
      result = pthread_cond_timedwait(&condition_, &mutex_, &absoluteRelease);
      if (result == ETIMEDOUT) {
        break;
      }
      if (result != std::int32_t{}) {
        (void)pthread_mutex_unlock(&mutex_);
        return result;
      }
    }
    const bool stopped = stopRequested_;
    result = pthread_mutex_unlock(&mutex_);
    if (result != std::int32_t{}) {
      return result;
    }
    if (stopped) {
      return ECANCELED;
    }
  }

  return std::int32_t{};
}

void PeriodicWait::requestStop() noexcept {
  if (!valid() || pthread_mutex_lock(&mutex_) != std::int32_t{}) {
    return;
  }
  stopRequested_ = true;
  (void)pthread_cond_broadcast(&condition_);
  (void)pthread_mutex_unlock(&mutex_);
}

}  // namespace common
