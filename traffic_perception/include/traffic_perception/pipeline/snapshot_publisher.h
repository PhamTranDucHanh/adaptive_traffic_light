#ifndef TRAFFIC_PERCEPTION_PIPELINE_SNAPSHOT_PUBLISHER_H_
#define TRAFFIC_PERCEPTION_PIPELINE_SNAPSHOT_PUBLISHER_H_

#include <string>

#include "traffic_perception/core/types.h"
#include "traffic_perception/io/snapshot_sender.h"
#include "traffic_perception/io/telemetry_manager.h"

class SnapshotPublisher {
 public:
  std::string BrokerUrl{};

  void initTelemetry(traffic_perception::TelemetryManager* telemetry);
  void initSender(traffic_perception::ISnapshotSender* sender);
  bool broadcastSnapshot(FrameContext& context);

 private:
  traffic_perception::TelemetryManager* telemetry_{nullptr};
  traffic_perception::ISnapshotSender* sender_{nullptr};
};

#endif  // TRAFFIC_PERCEPTION_PIPELINE_SNAPSHOT_PUBLISHER_H_
