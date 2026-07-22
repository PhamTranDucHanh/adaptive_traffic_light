#include "timing_plan_publisher.h"

#include <string_view>

#include "application_logger.h"
#include "common.h"

//
// Constructor
//
TimingPlanPublisher::TimingPlanPublisher()
    : queue_{traffic_ipc::kTimingPlanQueueName,
             traffic_ipc::kTimingPlanLockName} {}

bool TimingPlanPublisher::initialize() {
  // signal_control_demo depends on timing_decision, so no downstream consumer
  // is active while this owned channel is reset during Lifecycle startup.
  const auto status = queue_.open(true);
  if (status != traffic_ipc::QueueStatus::kSuccess) {
    traffic_timing_decision::applicationLogger().LogError()
        << "[IPC][PLAN][OPEN] status="
        << std::string_view{traffic_ipc::queueStatusName(status)}
        << "; errno=" << queue_.lastError();
    return false;
  }
  traffic_timing_decision::applicationLogger().LogInfo()
      << "[IPC][PLAN][OPEN] queue=" << traffic_ipc::kTimingPlanQueueName
      << "; mode=nonblocking_latest_value_publisher";
  return true;
}

void TimingPlanPublisher::shutdown() { queue_.close(); }

//
// Publish
//
bool TimingPlanPublisher::publishTimingPlan(const TimingPlan& plan) {
  const auto status = queue_.publish(plan);
  if (status == traffic_ipc::QueueStatus::kSuccess) {
    lastPublishedPlan = plan;
    lastPublishTimestampNs = common::monotonicNanoseconds();
    return true;
  }
  if (status == traffic_ipc::QueueStatus::kDeferred) {
    traffic_timing_decision::applicationLogger().LogWarn()
        << "[IPC][PLAN][DEFERRED] plan_id=" << plan.planId
        << "; reason=shared_lock_busy; retry=next_cycle";
    return true;
  }

  traffic_timing_decision::applicationLogger().LogError()
      << "[IPC][PLAN][SEND] status="
      << std::string_view{traffic_ipc::queueStatusName(status)}
      << "; errno=" << queue_.lastError() << "; plan_id=" << plan.planId;
  return false;
}

bool TimingPlanPublisher::publishPreviousTimingPlan() {
  if (lastPublishedPlan.planId == std::uint64_t{}) {
    return false;
  }
  return publishTimingPlan(lastPublishedPlan);
}
