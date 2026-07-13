#include "traffic_perception/pipeline/snapshot_publisher.h"

#include <iostream>

#include "traffic_perception/io/snapshot_sender.h"
#include "traffic_perception/io/telemetry_manager.h"

void SnapshotPublisher::initTelemetry(TelemetryManager *telemetryManager) {
  std::cout << "[SnapshotPublisher] initTelemetry() called" << '\n';
  TelemetryPtr = telemetryManager;
}

void SnapshotPublisher::initSender(ISnapshotSender *snapshotSender) {
  std::cout << "[SnapshotPublisher] initSender() called" << '\n';
  SenderPtr = snapshotSender;
}

void SnapshotPublisher::broadcastSnapshot(FrameContext &ctx) {
  std::cout << "[SnapshotPublisher] broadcastSnapshot() called for Lane: "
            << ctx.LaneId << ", FrameId: " << ctx.FrameId << '\n';
  if (SenderPtr != nullptr) {
    SenderPtr->send(ctx);
  }
  if (TelemetryPtr != nullptr) {
    TelemetryPtr->showTelemetryMetrics(ctx);
  }
}
