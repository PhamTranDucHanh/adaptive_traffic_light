#include "traffic_signal_controller/publisher_collector.h"

void PublisherCollector::receiveSignalOutput(const SignalOutput& output) {
  // TODO
}

bool PublisherCollector::detectCycleComplete(PhaseId phaseId) { return false; }

bool PublisherCollector::detectEmergencyTransition(bool isEmergency) {
  return false;
}

DataCollect PublisherCollector::packageData() { return {}; }

void PublisherCollector::publishToAnalytics(const DataCollect& data) {}

HealthStatus PublisherCollector::sendHeartbeat() const { return {}; }