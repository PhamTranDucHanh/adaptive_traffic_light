#include "traffic_signal_controller/timer.h"

Timer::Timer() {}

TimerTick Timer::tick() { return {}; }

uint64_t Timer::computeTheoreticalTimestamp() { return 0; }

HealthStatus Timer::sendHeartbeat() const { return {}; }