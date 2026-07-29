#ifndef IO_SNAPSHOT_SENDER_H
#define IO_SNAPSHOT_SENDER_H

#include <mqueue.h>

#include "traffic_perception/core/types.h"

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
  // POSIX message-queue descriptor; (mqd_t)-1 means not open.
  mqd_t mqDescriptor{static_cast<mqd_t>(-1)};
  // Fixed queue name; defined in the .cpp.
  const char* queueName{nullptr};
  struct mq_attr attributes{};
};

}  // namespace traffic_perception

#endif  // IO_SNAPSHOT_SENDER_H
