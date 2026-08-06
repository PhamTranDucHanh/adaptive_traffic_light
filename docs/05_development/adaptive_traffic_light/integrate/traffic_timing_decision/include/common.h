#ifndef TRAFFIC_TIMING_DECISION_COMMON_H
#define TRAFFIC_TIMING_DECISION_COMMON_H

#include <cstdint>
#include <ctime>

namespace common {

bool monotonicNow(timespec& value) noexcept;
std::uint64_t monotonicNanoseconds() noexcept;

}  // namespace common

#endif  // TRAFFIC_TIMING_DECISION_COMMON_H
