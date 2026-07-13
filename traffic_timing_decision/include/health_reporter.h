#ifndef HEALTH_REPORTER_H
#define HEALTH_REPORTER_H

#include <cstdint>

#include "decision_types.h"

enum class DeadlineStatus { Met, Missed };

class HealthReporter {
 public:
  HealthReporter();
  ~HealthReporter() = default;
  
  //----------------------------------------
  // decision cycle
  //----------------------------------------
  void startDecisionCycle();
  void finishDecisionCycle();
  //----------------------------------------
  // report
  //----------------------------------------
  HealthReport generateHealthReport() const;
  bool publishHealthReport();

 private:
  void recordStartTimestamp();
  void recordFinishTimestamp();
  void calculateExecutionTime();
  void evaluateDeadline();
  uint64_t cycleStartTimestampNs_{0};
  uint64_t cycleFinishTimestampNs_{0};
  uint64_t executionTimeNs_{0};
  uint32_t deadlineMs_{2500};
  DeadlineStatus deadlineStatus_{DeadlineStatus::Met};
  bool heartbeat_{false};
};

#endif  // !HEALTH_REPORTER_H