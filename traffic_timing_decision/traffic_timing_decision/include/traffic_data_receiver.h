#ifndef TRAFFIC_DATA_RECEIVER_H
#define TRAFFIC_DATA_RECEIVER_H

#include <cstdint>

#include "decision_types.h"
#include "traffic_ipc/latest_value_queue.h"

//
// Internal helper
//
class SnapshotQueue {
 public:
  SnapshotQueue();
  ~SnapshotQueue() = default;
  bool open();
  void close();
  bool receive(TrafficSnapshot& snapshot);
  traffic_ipc::QueueStatus lastStatus() const noexcept;
  int lastError() const noexcept;

 private:
  traffic_ipc::LatestValueConsumer<TrafficSnapshot> queue_;
  traffic_ipc::QueueStatus lastStatus_{traffic_ipc::QueueStatus::kNotFound};
};

//
// Traffic Data Receiver
//
class TrafficDataReceiver {
 public:
  TrafficDataReceiver();
  ~TrafficDataReceiver() = default;
  //----------------------------------------
  // lifecycle
  //----------------------------------------
  bool initialize();
  void shutdown();
  //----------------------------------------
  // snapshot
  //----------------------------------------
  bool requestSnapshot(TrafficSnapshot& snapshot);
  bool validateSnapshot(const TrafficSnapshot& snapshot);
  TrafficSnapshot getLatestSnapshot() const;
  traffic_ipc::QueueStatus lastQueueStatus() const noexcept;
  int lastQueueError() const noexcept;

 private:
  SnapshotQueue snapshotQueue;
  TrafficSnapshot latestSnapshot;
  TrafficSnapshot previousSnapshot;
};

#endif  // !TRAFFIC_DATA_RECEIVER_H
