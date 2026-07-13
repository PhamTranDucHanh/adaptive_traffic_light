#ifndef TIMING_PLAN_PUBLISHER_H
#define TIMING_PLAN_PUBLISHER_H

#include <cstdint>

#include "decision_types.h"

class TimingPlanPublisher {
 public:
  TimingPlanPublisher();
  ~TimingPlanPublisher() = default;
  //----------------------------------------
  // publish
  //----------------------------------------
  bool publishTimingPlan(const TimingPlan& plan);
  bool publishPreviousTimingPlan();

 private:
  bool waitAck();
  TimingPlan lastPublishedPlan;
  uint32_t ackTimeoutMs{100};
  uint64_t lastPublishTimestampNs{0};
};

#endif  // !TIMING_PLAN_PUBLISHER_H