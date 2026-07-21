#include "traffic_perception/io/snapshot_sender.h"

#include <iostream>

namespace traffic_perception {

bool MQSnapshotSender::open() {
  std::cout << "[MQSnapshotSender] open() called" << '\n';
  return true;
}

bool MQSnapshotSender::send(FrameContext &snapshot) {
  std::cout << "[MQSnapshotSender] send() called, Vehicles detected: "
            << snapshot.Detections.size() << '\n';
  return true;
}

void MQSnapshotSender::close() {
  std::cout << "[MQSnapshotSender] close() called" << '\n';
}

}  // namespace traffic_perception
