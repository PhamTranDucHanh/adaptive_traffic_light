#ifndef TRAFFIC_SIGNAL_CONTROLLER_MQ_TIMING_PLAN_RECEIVER_WORKER_H_
#define TRAFFIC_SIGNAL_CONTROLLER_MQ_TIMING_PLAN_RECEIVER_WORKER_H_

#include <mqueue.h>

#include <atomic>
#include <cstdint>

#include "traffic_signal_controller/plan_receiver.h"

class MqTimingPlanReceiverWorker final {
 public:
  explicit MqTimingPlanReceiverWorker(PlanReceiver& planReceiver) noexcept;
  ~MqTimingPlanReceiverWorker();

  MqTimingPlanReceiverWorker(const MqTimingPlanReceiverWorker&) = delete;
  MqTimingPlanReceiverWorker& operator=(const MqTimingPlanReceiverWorker&) =
      delete;

  bool Run(const std::atomic_bool& running) noexcept;
  void Close() noexcept;

 private:
  bool OpenAndValidate() noexcept;
  bool ReceiveAndProcessNewest(const std::atomic_bool& running) noexcept;
  bool IsNewerTransportMessage(std::uint64_t publisherInstanceId,
                               std::uint64_t sequenceNumber) noexcept;

  PlanReceiver& planReceiver_;
  mqd_t queueDescriptor_{static_cast<mqd_t>(-1)};
  bool hasTransportPosition_{false};
  std::uint64_t publisherInstanceId_{0U};
  std::uint64_t lastSequenceNumber_{0U};
};

#endif  // TRAFFIC_SIGNAL_CONTROLLER_MQ_TIMING_PLAN_RECEIVER_WORKER_H_
