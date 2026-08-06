#include "common.h"

namespace {

enum class TimeConversion : std::uint64_t {
  kNanosecondsPerMillisecond = 1000000ULL,
  kNanosecondsPerSecond = 1000000000ULL,
};

constexpr std::uint64_t toNanoseconds(const TimeConversion value) noexcept {
  return static_cast<std::uint64_t>(value);
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

}  // namespace common
