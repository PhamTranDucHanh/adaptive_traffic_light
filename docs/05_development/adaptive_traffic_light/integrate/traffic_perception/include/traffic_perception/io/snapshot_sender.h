#ifndef IO_SNAPSHOT_SENDER_H
#define IO_SNAPSHOT_SENDER_H

#include "traffic_perception/core/types.h"
#include "traffic_ipc/latest_value_queue.h"
#include "traffic_ipc/messages.h"

namespace traffic_perception {

class ISnapshotSender {
 public:
  virtual ~ISnapshotSender() = default;
  virtual bool open() = 0;
  virtual bool send(const TrafficSnapshot& snapshot) = 0;
  virtual void close() = 0;
};

class MQSnapshotSender : public ISnapshotSender {
 public:
  MQSnapshotSender();
  ~MQSnapshotSender() override;

  bool open() override;
  bool send(const TrafficSnapshot& snapshot) override;
  void close() override;

 private:
  traffic_ipc::LatestValuePublisher<traffic_ipc::TrafficSnapshot> publisher_{
      traffic_ipc::kTrafficSnapshotQueueName,
      traffic_ipc::kTrafficSnapshotLockName};
  bool open_{false};
};

}  // namespace traffic_perception

#endif  // IO_SNAPSHOT_SENDER_H
