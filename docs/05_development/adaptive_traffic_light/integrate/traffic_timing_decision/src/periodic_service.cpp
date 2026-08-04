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

  consecutiveSnapshotMisses_ = std::uint32_t{};
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

std::uint32_t PeriodicService::periodMs() const { return periodMs_; }

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
    const traffic_ipc::QueueStatus status = trafficReceiver.lastQueueStatus();
    if (status == traffic_ipc::QueueStatus::kEmpty ||
        status == traffic_ipc::QueueStatus::kBusy) {
      traffic_timing_decision::ipcLogger().LogWarn()
          << "[IPC][SNAPSHOT][NO_NEW_DATA] status="
          << std::string_view{traffic_ipc::queueStatusName(status)}
          << "; consecutive_misses=" << consecutiveSnapshotMisses_
          << "; action=keep_previous_plan";
    } else {
      traffic_timing_decision::ipcLogger().LogError()
          << "[IPC][SNAPSHOT][RECEIVE] status="
          << std::string_view{traffic_ipc::queueStatusName(status)}
          << "; errno=" << trafficReceiver.lastQueueError()
          << "; consecutive_misses=" << consecutiveSnapshotMisses_;
    }
    cycleSuccessful = timingPublisher.retryPendingTimingPlan();
  } else if (!trafficReceiver.validateSnapshot(snapshot)) {
    ++consecutiveSnapshotMisses_;
    traffic_timing_decision::ipcLogger().LogWarn()
        << "[IPC][SNAPSHOT][REJECTED] frame_id=" << snapshot.frameId
        << "; consecutive_misses=" << consecutiveSnapshotMisses_
        << "; action=keep_previous_plan";
    cycleSuccessful = timingPublisher.retryPendingTimingPlan();
  } else {
    consecutiveSnapshotMisses_ = std::uint32_t{};
    const TimingPlan plan = decisionEngine.processTrafficMetrics(snapshot);
    cycleSuccessful = timingPublisher.publishTimingPlan(plan);
    if (cycleSuccessful) {
      traffic_timing_decision::decisionLogger().LogInfo()
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
    traffic_timing_decision::ipcLogger().LogError()
        << "[DECISION][INPUT_TIMEOUT] consecutive_misses="
        << consecutiveSnapshotMisses_
        << "; threshold=" << maximumConsecutiveSnapshotMisses_;
    return false;
  }
  return cycleSuccessful;
}
