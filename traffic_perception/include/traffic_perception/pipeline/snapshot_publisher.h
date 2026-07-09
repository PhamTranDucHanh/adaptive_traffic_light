#ifndef PIPELINE_SNAPSHOT_PUBLISHER_H
#define PIPELINE_SNAPSHOT_PUBLISHER_H

#include <string>

#include "traffic_perception/core/types.h"

// Forward declarations
class TelemetryManager;
class ISnapshotSender;

class SnapshotPublisher {
 public:
  std::string BrokerUrl;

 private:
  TelemetryManager *TelemetryPtr;
  ISnapshotSender *SenderPtr;

 public:
  void initTelemetry(TelemetryManager *t);
  void initSender(ISnapshotSender *s);
  void broadcastSnapshot(FrameContext &ctx);
};

#endif  // PIPELINE_SNAPSHOT_PUBLISHER_H
