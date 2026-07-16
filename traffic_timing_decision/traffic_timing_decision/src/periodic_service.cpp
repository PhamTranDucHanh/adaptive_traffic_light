#include "periodic_service.h"

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

  if (!healthReporter.initialize()) {
    trafficReceiver.shutdown();
    return false;
  }

  running_ = true;
  return true;
}

void PeriodicService::shutdown() {
  if (!running_) {
    return;
  }

  healthReporter.shutdown();
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

  const TrafficSnapshot snapshot = trafficReceiver.requestSnapshot();
  if (trafficReceiver.validateSnapshot(snapshot)) {
    const TimingPlan plan = decisionEngine.processTrafficMetrics(snapshot);
    if (!timingPublisher.publishTimingPlan(plan)) {
      (void)timingPublisher.publishPreviousTimingPlan();
    }
  }

  healthReporter.finishDecisionCycle();
  return true;
}
