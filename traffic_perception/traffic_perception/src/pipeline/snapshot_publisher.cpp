#include "traffic_perception/pipeline/snapshot_publisher.h"

#include <iostream>

namespace traffic_perception {

void SnapshotPublisher::initSender(ISnapshotSender* sender) {
  sender_ = sender;
}

bool SnapshotPublisher::broadcastSnapshot(const FrameContext& ctx) {
  Timeline tl = ctx.timeline;
  tl.add(TimelineStage::Publish);
  logger_.log(tl);
  
  if (sender_ == nullptr) {
    std::cerr << "[PERCEPTION][PUBLISH][ERROR] sender is null\n";
    return false;
  }
  // Forward the snapshot to the sender.
  return sender_->send(ctx.snapshot);
}

void SnapshotPublisher::flush() {
  logger_.dump();
}

}  // namespace traffic_perception
