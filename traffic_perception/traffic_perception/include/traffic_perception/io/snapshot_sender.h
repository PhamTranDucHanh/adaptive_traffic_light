#ifndef IO_SNAPSHOT_SENDER_H
#define IO_SNAPSHOT_SENDER_H

#include <mqueue.h>

#include "traffic_perception/core/types.h"

namespace traffic_perception {

class ISnapshotSender {
 public:
  virtual ~ISnapshotSender() = default;
  virtual bool open() = 0;
  virtual bool send(FrameContext &snapshot) = 0;
  virtual void close() = 0;
};

class MQSnapshotSender : public ISnapshotSender {
 private:
  mqd_t mqDescriptor;
  char *queueName;
  struct mq_attr attributes;

 public:
  bool open() override;
  bool send(FrameContext &snapshot) override;
  void close() override;
};

}  // namespace traffic_perception

#endif  // IO_SNAPSHOT_SENDER_H
