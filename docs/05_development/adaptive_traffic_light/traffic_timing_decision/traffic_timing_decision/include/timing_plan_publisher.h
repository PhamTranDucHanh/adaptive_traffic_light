#ifndef TIMING_PLAN_PUBLISHER_H
#define TIMING_PLAN_PUBLISHER_H

#include <cstdint>
#include <mqueue.h>
#include <optional>

#include "decision_types.h"
#include "traffic_ipc/timing_plan_message_v1.h"

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
  bool retryPending();

 private:
  bool trySendPending();
  static std::uint64_t createPublisherInstanceId() noexcept;
  traffic_ipc::TimingPlanMessageV1 makeMessage(const TimingPlan& plan) noexcept;

  mqd_t queueDescriptor_{static_cast<mqd_t>(-1)};
  std::optional<traffic_ipc::TimingPlanMessageV1> pendingMessage_{};
  std::uint64_t publisherInstanceId_{0U};
  std::uint64_t nextSequenceNumber_{1U};
  TimingPlan lastPublishedPlan;
  std::uint64_t lastPublishTimestampNs{};
  int lastError_{0};
};

#endif  // !TIMING_PLAN_PUBLISHER_H
