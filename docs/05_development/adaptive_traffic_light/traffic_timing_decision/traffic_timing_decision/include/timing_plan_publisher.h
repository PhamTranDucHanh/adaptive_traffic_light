#ifndef TIMING_PLAN_PUBLISHER_H
#define TIMING_PLAN_PUBLISHER_H

#include <cstdint>

#include "decision_types.h"
#include "traffic_ipc/latest_value_queue.h"

class TimingPlanPublisher {
 public:
  TimingPlanPublisher();
  ~TimingPlanPublisher() = default;
  bool initialize();
  void shutdown();
  //----------------------------------------
  // publish
  //----------------------------------------
  bool publishTimingPlan(const TimingPlan& plan);
  bool publishPreviousTimingPlan();

 private:
  traffic_ipc::LatestValuePublisher<TimingPlan> queue_;
  TimingPlan lastPublishedPlan;
  std::uint64_t lastPublishTimestampNs{};
};

#endif  // !TIMING_PLAN_PUBLISHER_H
