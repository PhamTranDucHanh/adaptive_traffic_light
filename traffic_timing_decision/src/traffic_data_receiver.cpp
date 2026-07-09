#include "traffic_data_receiver.h"

//
// SnapshotQueue
//
SnapshotQueue::SnapshotQueue() = default;

bool SnapshotQueue::open() { return true; }
void SnapshotQueue::close() {}
bool SnapshotQueue::receive(TrafficSnapshot& snapshot) {
  (void)snapshot;
  return true;
}

//
// TrafficDataReceiver
//
TrafficDataReceiver::TrafficDataReceiver() = default;

bool TrafficDataReceiver::initialize() { return snapshotQueue.open(); }
void TrafficDataReceiver::shutdown() { snapshotQueue.close(); }

TrafficSnapshot TrafficDataReceiver::requestSnapshot() {
  return TrafficSnapshot{};
}

bool TrafficDataReceiver::validateSnapshot(const TrafficSnapshot& snapshot) {
  (void)snapshot;
  return true;
}

TrafficSnapshot TrafficDataReceiver::getLatestSnapshot() const {
  return latestSnapshot;
}