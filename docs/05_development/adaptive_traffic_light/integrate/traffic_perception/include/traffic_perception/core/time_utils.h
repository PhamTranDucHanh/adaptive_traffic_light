#pragma once

#include <cstdint>
#include <ctime>

namespace traffic_perception {

inline int64_t GetMonotonicTimeNs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL +
           static_cast<int64_t>(ts.tv_nsec);
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
