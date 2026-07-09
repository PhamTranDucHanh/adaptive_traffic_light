#include "traffic_perception/io/snapshot_sender.h"

#include <iostream>

bool MQSnapshotSender::open() {
  std::cout << "[MQSnapshotSender] open() called" << '\n';
  return true;
}

bool MQSnapshotSender::send(FrameContext &snapshot) {
  std::cout << "[MQSnapshotSender] send() called for FrameId: "
            << snapshot.FrameId
            << ", Vehicles detected: " << snapshot.VehicleCount << '\n';
  return true;
}

void MQSnapshotSender::close() {
  std::cout << "[MQSnapshotSender] close() called" << '\n';
}
