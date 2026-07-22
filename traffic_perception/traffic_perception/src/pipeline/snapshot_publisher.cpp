#include "traffic_perception/pipeline/snapshot_publisher.h"

#include <iostream>

namespace traffic_perception {

void SnapshotPublisher::initSender(ISnapshotSender* sender) {
  sender_ = sender;
}

bool SnapshotPublisher::broadcastSnapshot(const TrafficSnapshot& snapshot) {
  logger_.log(snapshot);
  
  if (sender_ == nullptr) {
    std::cerr << "[PERCEPTION][PUBLISH][ERROR] sender is null\n";
    return false;
  }
  // Forward the snapshot to the sender.
  return sender_->send(snapshot);
}

void SnapshotPublisher::flush() {
  logger_.dump();
}

}  // namespace traffic_perception
