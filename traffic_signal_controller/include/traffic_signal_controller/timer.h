#ifndef TIMER_H
#define TIMER_H

#include <common/config.h>

class Timer {
 public:
  Timer();

  TimerTick tick();

  HealthStatus sendHeartbeat() const;

 private:
  uint32_t intervalMs;
  uint64_t tickCount;
  uint64_t startTime;

  uint64_t computeTheoreticalTimestamp();
};

#endif