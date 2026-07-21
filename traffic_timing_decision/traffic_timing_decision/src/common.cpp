#include "common.h"

#include <cerrno>

namespace {

constexpr long kNanosecondsPerSecond = 1000000000L;

bool isAfter(const timespec& left, const timespec& right) noexcept {
  return left.tv_sec > right.tv_sec ||
         (left.tv_sec == right.tv_sec && left.tv_nsec > right.tv_nsec);
}

}  // namespace

namespace common {

bool monotonicNow(timespec& value) noexcept {
  return clock_gettime(CLOCK_MONOTONIC, &value) == 0;
}

std::uint64_t monotonicNanoseconds() noexcept {
  timespec now{};
  if (!monotonicNow(now)) {
    return 0U;
  }
  return static_cast<std::uint64_t>(now.tv_sec) * 1000000000ULL +
         static_cast<std::uint64_t>(now.tv_nsec);
}

void addMilliseconds(timespec& value,
                     const std::uint32_t milliseconds) noexcept {
  const std::uint64_t additionalNanoseconds =
      static_cast<std::uint64_t>(milliseconds) * 1000000ULL;
  value.tv_sec +=
      static_cast<time_t>(additionalNanoseconds / kNanosecondsPerSecond);
  value.tv_nsec +=
      static_cast<long>(additionalNanoseconds % kNanosecondsPerSecond);
  if (value.tv_nsec >= kNanosecondsPerSecond) {
    ++value.tv_sec;
    value.tv_nsec -= kNanosecondsPerSecond;
  }
}

std::uint32_t advancePastNow(timespec& release, const std::uint32_t periodMs,
                             const timespec& now) noexcept {
  std::uint32_t skipped{0U};
  while (!isAfter(release, now)) {
    addMilliseconds(release, periodMs);
    ++skipped;
  }
  return skipped;
}

PeriodicWait::PeriodicWait() noexcept {
  if (pthread_mutex_init(&mutex_, nullptr) != 0) {
    return;
  }
  mutexReady_ = true;

  pthread_condattr_t attributes{};
  if (pthread_condattr_init(&attributes) != 0) {
    return;
  }
  const int clockResult =
      pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
  if (clockResult == 0 && pthread_cond_init(&condition_, &attributes) == 0) {
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

int PeriodicWait::waitUntil(const timespec& absoluteRelease) noexcept {
  if (!valid()) {
    return EINVAL;
  }

  timespec now{};
  if (!monotonicNow(now)) {
    return errno != 0 ? errno : EINVAL;
  }

  if (isAfter(absoluteRelease, now)) {
    int result = pthread_mutex_lock(&mutex_);
    if (result != 0) {
      return result;
    }

    while (!stopRequested_) {
      result = pthread_cond_timedwait(&condition_, &mutex_, &absoluteRelease);
      if (result == ETIMEDOUT) {
        break;
      }
      if (result != 0) {
        (void)pthread_mutex_unlock(&mutex_);
        return result;
      }
    }
    const bool stopped = stopRequested_;
    result = pthread_mutex_unlock(&mutex_);
    if (result != 0) {
      return result;
    }
    if (stopped) {
      return ECANCELED;
    }
  }

  return 0;
}

void PeriodicWait::requestStop() noexcept {
  if (!valid() || pthread_mutex_lock(&mutex_) != 0) {
    return;
  }
  stopRequested_ = true;
  (void)pthread_cond_broadcast(&condition_);
  (void)pthread_mutex_unlock(&mutex_);
}

}  // namespace common
