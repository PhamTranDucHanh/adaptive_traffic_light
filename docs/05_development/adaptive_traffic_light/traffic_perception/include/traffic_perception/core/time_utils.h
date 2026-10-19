#pragma once

#include <cerrno>
#include <cstdint>
#include <ctime>

namespace traffic_perception {

inline int64_t GetMonotonicTimeNs() noexcept {
    timespec ts{};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL +
           static_cast<int64_t>(ts.tv_nsec);
}

inline std::uint64_t GetMonotonicTimeUs() noexcept {
    constexpr std::int64_t kNanosecondsPerMicrosecond{1000LL};
    const std::int64_t timestampNs = GetMonotonicTimeNs();
    return timestampNs > 0
               ? static_cast<std::uint64_t>(timestampNs /
                                            kNanosecondsPerMicrosecond)
               : 0U;
}

inline timespec ToTimespec(int64_t ns) {
    timespec ts;
    ts.tv_sec = ns / 1000000000LL;
    ts.tv_nsec = ns % 1000000000LL;
    return ts;
}

inline void SleepUntilNs(int64_t wakeupNs) {
    timespec ts = ToTimespec(wakeupNs);
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == EINTR) {
    }
}

}  // namespace traffic_perception
