#ifndef TRAFFIC_DATA_RECEIVER_H
#define TRAFFIC_DATA_RECEIVER_H

#include <mqueue.h>

#include <cstdint>

#include "decision_types.h"
#include "traffic_ipc/latest_value_queue.h"

enum class SnapshotWaitStatus : std::uint8_t {
  kReady,
  kTimeout,
  kStopped,
  kRetry,
  kError,
};

//
// Internal helper
//
class SnapshotQueue {
 public:
  SnapshotQueue();
  ~SnapshotQueue() = default;
  bool open();
  void close();
  SnapshotWaitStatus waitAndReceive(TrafficSnapshot& snapshot,
                                    std::uint32_t timeoutMs) noexcept;
  void requestStop() noexcept;
  traffic_ipc::QueueStatus lastStatus() const noexcept;
  std::int32_t lastError() const noexcept;

 private:
  traffic_ipc::LatestValueConsumer<TrafficSnapshot> queue_;
  mqd_t readinessDescriptor_{static_cast<mqd_t>(-1)};
  std::int32_t stopEventDescriptor_{-1};
  traffic_ipc::QueueStatus lastStatus_{traffic_ipc::QueueStatus::kNotFound};
  std::int32_t lastError_{0};
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
  SnapshotWaitStatus waitForSnapshot(TrafficSnapshot& snapshot,
                                     std::uint32_t timeoutMs) noexcept;
  void requestStop() noexcept;
  bool validateSnapshot(const TrafficSnapshot& snapshot);
  TrafficSnapshot getLatestSnapshot() const;
  traffic_ipc::QueueStatus lastQueueStatus() const noexcept;
  std::int32_t lastQueueError() const noexcept;

 private:
  SnapshotQueue snapshotQueue;
  TrafficSnapshot latestSnapshot;
  TrafficSnapshot previousSnapshot;
};

#endif  // !TRAFFIC_DATA_RECEIVER_H
