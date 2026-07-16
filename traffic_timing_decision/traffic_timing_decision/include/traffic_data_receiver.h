#ifndef TRAFFIC_DATA_RECEIVER_H
#define TRAFFIC_DATA_RECEIVER_H

#include <cstdint>
#include <string>

#include "decision_types.h"

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

 private:
  int32_t mqDescriptor{-1};
  std::string queueName;
  uint32_t maximumMessageSize{0};
  uint32_t maximumMessageCount{0};
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
  TrafficSnapshot requestSnapshot();
  bool validateSnapshot(const TrafficSnapshot& snapshot);
  TrafficSnapshot getLatestSnapshot() const;

 private:
  SnapshotQueue snapshotQueue;
  TrafficSnapshot latestSnapshot;
  TrafficSnapshot previousSnapshot;
  uint32_t snapshotTimeoutMs{100};
  uint64_t lastReceiveTimestampNs{0};
};

#endif  // !TRAFFIC_DATA_RECEIVER_H