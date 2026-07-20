#include "periodic_service.h"

#include <string_view>

#include "application_logger.h"
#include "traffic_ipc/latest_value_queue.h"

//
// Constructor
//
PeriodicService::PeriodicService() = default;

bool PeriodicService::initialize() {
  if (running_) {
    return true;
  }

  if (!trafficReceiver.initialize()) {
    return false;
  }

  if (!timingPublisher.initialize()) {
    trafficReceiver.shutdown();
    return false;
  }

  if (!healthReporter.initialize()) {
    timingPublisher.shutdown();
    trafficReceiver.shutdown();
    return false;
  }

  consecutiveSnapshotMisses_ = 0U;
  running_ = true;
  return true;
}

void PeriodicService::shutdown() {
  if (!running_) {
    return;
  }

  healthReporter.shutdown();
  timingPublisher.shutdown();
  trafficReceiver.shutdown();
  running_ = false;
}

uint32_t PeriodicService::periodMs() const { return periodMs_; }

//
// Decision Pipeline
//
bool PeriodicService::runDecisionCycle() {
  if (!running_) {
    return false;
  }

  if (!healthReporter.startDecisionCycle()) {
    return false;
  }

  bool cycleSuccessful{true};
  TrafficSnapshot snapshot{};
  if (!trafficReceiver.requestSnapshot(snapshot)) {
    ++consecutiveSnapshotMisses_;
    const auto status = trafficReceiver.lastQueueStatus();
    if (status == traffic_ipc::QueueStatus::kEmpty ||
        status == traffic_ipc::QueueStatus::kBusy) {
      traffic_timing_decision::applicationLogger().LogWarn()
          << "[IPC][SNAPSHOT][NO_NEW_DATA] status="
          << std::string_view{traffic_ipc::queueStatusName(status)}
          << "; consecutive_misses=" << consecutiveSnapshotMisses_
          << "; action=keep_previous_plan";
    } else {
      traffic_timing_decision::applicationLogger().LogError()
          << "[IPC][SNAPSHOT][RECEIVE] status="
          << std::string_view{traffic_ipc::queueStatusName(status)}
          << "; errno=" << trafficReceiver.lastQueueError()
          << "; consecutive_misses=" << consecutiveSnapshotMisses_;
    }
  } else if (!trafficReceiver.validateSnapshot(snapshot)) {
    ++consecutiveSnapshotMisses_;
    traffic_timing_decision::applicationLogger().LogWarn()
        << "[IPC][SNAPSHOT][REJECTED] frame_id=" << snapshot.frameId
        << "; consecutive_misses=" << consecutiveSnapshotMisses_
        << "; action=keep_previous_plan";
  } else {
    consecutiveSnapshotMisses_ = 0U;
    const TimingPlan plan = decisionEngine.processTrafficMetrics(snapshot);
    cycleSuccessful = timingPublisher.publishTimingPlan(plan);
    if (cycleSuccessful) {
      traffic_timing_decision::applicationLogger().LogInfo()
          << "[DECISION][PUBLISHED] frame_id=" << snapshot.frameId
          << "; plan_id=" << plan.planId
          << "; ns_green_ms=" << plan.greenNorthSouthMs
          << "; ew_green_ms=" << plan.greenEastWestMs
          << "; cycle_ms=" << plan.cycleLengthMs
          << "; emergency_ns=" << plan.emergencyNorthSouth
          << "; emergency_ew=" << plan.emergencyEastWest;
    }
  }

  healthReporter.finishDecisionCycle();
  if (consecutiveSnapshotMisses_ >= maximumConsecutiveSnapshotMisses_) {
    traffic_timing_decision::applicationLogger().LogError()
        << "[DECISION][INPUT_TIMEOUT] consecutive_misses="
        << consecutiveSnapshotMisses_
        << "; threshold=" << maximumConsecutiveSnapshotMisses_;
    return false;
  }
  return cycleSuccessful;
}
