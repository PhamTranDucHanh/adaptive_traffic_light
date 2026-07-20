#include "traffic_perception/pipeline/snapshot_publisher.h"

#include <iostream>

using namespace traffic_perception;

void SnapshotPublisher::initTelemetry(TelemetryManager* telemetry) {
  telemetry_ = telemetry;
}

void SnapshotPublisher::initSender(ISnapshotSender* sender) {
  sender_ = sender;
}

bool SnapshotPublisher::broadcastSnapshot(FrameContext& context) {
  if (sender_ == nullptr) {
    std::cerr << "[PERCEPTION][PUBLISH][ERROR] sender is null\n";
    return false;
  }

  const bool sent = sender_->send(context);
  if (sent && telemetry_ != nullptr) {
    telemetry_->showTelemetryMetrics(context);
  }
  return sent;
}
