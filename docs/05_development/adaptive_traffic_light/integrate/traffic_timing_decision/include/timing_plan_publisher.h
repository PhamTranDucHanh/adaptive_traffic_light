#ifndef TIMING_PLAN_PUBLISHER_H
#define TIMING_PLAN_PUBLISHER_H

#include <mqueue.h>

#include <cstdint>
#include <optional>

#include "decision_types.h"
#include "traffic_ipc/timing_plan_message_v1.h"

class TimingPlanPublisher {
 public:
  TimingPlanPublisher();
  ~TimingPlanPublisher();

  TimingPlanPublisher(const TimingPlanPublisher&) = delete;
  TimingPlanPublisher& operator=(const TimingPlanPublisher&) = delete;

  bool initialize();
  void shutdown();
  //----------------------------------------
  // publish
  //----------------------------------------
  bool publishTimingPlan(const TimingPlan& plan);
  bool retryPendingTimingPlan();
  bool publishPreviousTimingPlan();
  bool wasLastPlanSuppressed() const noexcept;

 private:
  bool validateQueueContract() noexcept;
  traffic_ipc::TimingPlanMessageV1 makeMessage(
      const TimingPlan& plan) noexcept;
  bool trySendPending() noexcept;
  std::uint64_t nextSequenceNumber() noexcept;

  mqd_t descriptor_{static_cast<mqd_t>(-1)};
  std::optional<traffic_ipc::TimingPlanMessageV1> pendingLatest_{};
  std::optional<TimingPlan> pendingLatestPlan_{};
  TimingPlan lastPublishedPlan_{};
  std::uint64_t publisherInstanceId_{};
  std::uint64_t sequenceNumber_{};
  std::uint64_t lastPublishTimestampNs_{};
  std::int32_t lastError_{};
  bool lastPlanSuppressed_{false};
};

#endif  // !TIMING_PLAN_PUBLISHER_H
