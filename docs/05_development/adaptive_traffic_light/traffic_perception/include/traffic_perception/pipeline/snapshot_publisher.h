#ifndef TRAFFIC_PERCEPTION_PIPELINE_SNAPSHOT_PUBLISHER_H_
#define TRAFFIC_PERCEPTION_PIPELINE_SNAPSHOT_PUBLISHER_H_

#include "traffic_perception/core/types.h"
#include "traffic_perception/io/snapshot_sender.h"

namespace traffic_perception {

class SnapshotPublisher {
 public:
  // Initialize the sender that will actually transmit snapshots.
  void initSender(ISnapshotSender* sender);

  // Publish a traffic snapshot to the downstream consumer.
  // Returns true on success, false otherwise.
  bool broadcastSnapshot(const FrameContext& ctx);

 private:
  ISnapshotSender* sender_{nullptr};
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_PIPELINE_SNAPSHOT_PUBLISHER_H_
