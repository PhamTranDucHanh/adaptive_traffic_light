#include "traffic_signal_controller/mq_timing_plan_receiver_worker.h"

#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <mqueue.h>

#include <cstdint>
#include <cstring>
#include <string_view>

#include "common/config.h"
#include "common/logging_contexts.h"
#include "score/mw/log/logger.h"
#include "traffic_ipc/timing_plan_message_v1.h"

namespace {

score::mw::log::Logger& Logger() {
  static auto& logger = score::mw::log::CreateLogger(
      ctrl::logging::kCtxMqReceiver, "MQ Timing Plan Receiver");
  return logger;
}

bool MakeRealtimeDeadlineAfter(const long nanoseconds,
                               timespec& deadline) noexcept {
  if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
    return false;
  }

  const std::uint64_t totalNanoseconds =
      static_cast<std::uint64_t>(deadline.tv_nsec) +
      static_cast<std::uint64_t>(nanoseconds);
  deadline.tv_sec +=
      static_cast<time_t>(totalNanoseconds / kNanosecondsPerSecond);
  deadline.tv_nsec =
      static_cast<long>(totalNanoseconds % kNanosecondsPerSecond);
  return true;
}

void WaitBeforeOpenRetry(const std::atomic_bool& running) noexcept {
  timespec remaining{0, kTimingPlanOpenRetryNanoseconds};
  while (running.load(std::memory_order_acquire) &&
         nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {
  }
}

TimingPlan DecodePlan(
    const traffic_ipc::TimingPlanMessageV1& message) noexcept {
  TimingPlan plan{};
  plan.planId = message.planId;
  plan.generationTimestampNs = message.generationTimestampNs;
  plan.greenNorthSouthMs = message.greenNorthSouthMs;
  plan.greenEastWestMs = message.greenEastWestMs;
  plan.yellowMs = message.yellowMs;
  plan.allRedMs = message.allRedMs;
  plan.cycleLengthMs = message.cycleLengthMs;
  plan.emergencyNorthSouth = message.emergencyNorthSouth == 1U;
  plan.emergencyEastWest = message.emergencyEastWest == 1U;
  return plan;
}

}  // namespace

MqTimingPlanReceiverWorker::MqTimingPlanReceiverWorker(
    PlanReceiver& planReceiver) noexcept
    : planReceiver_{planReceiver} {}

MqTimingPlanReceiverWorker::~MqTimingPlanReceiverWorker() { Close(); }

bool MqTimingPlanReceiverWorker::Run(
    const std::atomic_bool& running) noexcept {
  while (running.load(std::memory_order_acquire)) {
    if (queueDescriptor_ == static_cast<mqd_t>(-1)) {
      if (!OpenAndValidate()) {
        if (errno != ENOENT) {
          return false;
        }
        WaitBeforeOpenRetry(running);
        continue;
      }
    }

    if (!ReceiveAndProcessNewest(running)) {
      Close();
      if (running.load(std::memory_order_acquire)) {
        WaitBeforeOpenRetry(running);
      }
    }
  }

  Close();
  return true;
}

void MqTimingPlanReceiverWorker::Close() noexcept {
  if (queueDescriptor_ != static_cast<mqd_t>(-1)) {
    (void)mq_close(queueDescriptor_);
    queueDescriptor_ = static_cast<mqd_t>(-1);
  }
}

bool MqTimingPlanReceiverWorker::OpenAndValidate() noexcept {
  queueDescriptor_ =
      mq_open(traffic_ipc::kTimingPlanQueueName, O_RDONLY | O_CLOEXEC);
  if (queueDescriptor_ == static_cast<mqd_t>(-1)) {
    return false;
  }

  mq_attr attributes{};
  if (mq_getattr(queueDescriptor_, &attributes) != 0) {
    const int errorNumber = errno;
    Logger().LogWarn() << "event=TIMING_PLAN_QUEUE_ATTRIBUTE_FAILED"
                       << ", error=" << errorNumber
                       << ", reason="
                       << std::string_view{std::strerror(errorNumber)};
    Close();
    errno = errorNumber;
    return false;
  }

  if (attributes.mq_maxmsg != traffic_ipc::kTimingPlanQueueMaxMessages ||
      attributes.mq_msgsize != traffic_ipc::kTimingPlanQueueMessageSize) {
    Logger().LogError()
        << "event=TIMING_PLAN_QUEUE_CONTRACT_MISMATCH"
        << ", queue=" << traffic_ipc::kTimingPlanQueueName
        << ", expected_maxmsg=" << traffic_ipc::kTimingPlanQueueMaxMessages
        << ", actual_maxmsg=" << attributes.mq_maxmsg
        << ", expected_msgsize=" << traffic_ipc::kTimingPlanQueueMessageSize
        << ", actual_msgsize=" << attributes.mq_msgsize;
    Close();
    errno = EMSGSIZE;
    return false;
  }

  Logger().LogInfo() << "event=TIMING_PLAN_QUEUE_OPENED"
                     << ", queue=" << traffic_ipc::kTimingPlanQueueName;
  return true;
}

bool MqTimingPlanReceiverWorker::ReceiveAndProcessNewest(
    const std::atomic_bool& running) noexcept {
  traffic_ipc::TimingPlanMessageV1 newest{};
  bool hasCandidate{false};
  bool firstReceive{true};

  while (running.load(std::memory_order_acquire)) {
    timespec deadline{};
    if (!MakeRealtimeDeadlineAfter(
            firstReceive ? kTimingPlanReceiveTimeoutNanoseconds : 0L,
            deadline)) {
      const int errorNumber = errno;
      Logger().LogWarn() << "event=TIMING_PLAN_DEADLINE_FAILED"
                         << ", error=" << errorNumber
                         << ", reason="
                         << std::string_view{std::strerror(errorNumber)};
      return false;
    }
    traffic_ipc::TimingPlanMessageV1 current{};
    const ssize_t receivedBytes =
        mq_timedreceive(queueDescriptor_, reinterpret_cast<char*>(&current),
                        sizeof(current), nullptr, &deadline);

    if (receivedBytes < 0) {
      const int errorNumber = errno;
      if (errorNumber == ETIMEDOUT || errorNumber == EINTR ||
          errorNumber == EAGAIN) {
        break;
      }
      Logger().LogWarn() << "event=TIMING_PLAN_RECEIVE_FAILED"
                         << ", error=" << errorNumber
                         << ", reason="
                         << std::string_view{std::strerror(errorNumber)};
      return false;
    }

    firstReceive = false;
    if (receivedBytes != static_cast<ssize_t>(sizeof(current))) {
      Logger().LogWarn() << "event=TIMING_PLAN_MESSAGE_REJECTED"
                         << ", reason=SIZE_MISMATCH"
                         << ", received_size=" << receivedBytes;
      continue;
    }

    if (!traffic_ipc::HasValidTimingPlanEnvelope(current)) {
      Logger().LogWarn() << "event=TIMING_PLAN_MESSAGE_REJECTED"
                         << ", reason=INVALID_ENVELOPE";
      continue;
    }

    if (!IsNewerTransportMessage(current.publisherInstanceId,
                                 current.sequenceNumber)) {
      Logger().LogWarn() << "event=TIMING_PLAN_MESSAGE_REJECTED"
                         << ", reason=STALE_SEQUENCE"
                         << ", publisher_instance_id="
                         << current.publisherInstanceId
                         << ", sequence=" << current.sequenceNumber;
      continue;
    }

    newest = current;
    hasCandidate = true;
  }

  if (!hasCandidate) {
    return true;
  }

  const TimingPlan plan = DecodePlan(newest);
  const bool accepted = planReceiver_.ReceivePlan(plan);
  Logger().LogInfo() << "event=TIMING_PLAN_PROCESSED"
                     << ", publisher_instance_id="
                     << newest.publisherInstanceId
                     << ", sequence=" << newest.sequenceNumber
                     << ", plan_id=" << newest.planId
                     << ", result=" << (accepted ? "accepted" : "rejected");
  return true;
}

bool MqTimingPlanReceiverWorker::IsNewerTransportMessage(
    const std::uint64_t publisherInstanceId,
    const std::uint64_t sequenceNumber) noexcept {
  if (!hasTransportPosition_ ||
      publisherInstanceId != publisherInstanceId_) {
    hasTransportPosition_ = true;
    publisherInstanceId_ = publisherInstanceId;
    lastSequenceNumber_ = sequenceNumber;
    return true;
  }

  if (sequenceNumber <= lastSequenceNumber_) {
    return false;
  }

  lastSequenceNumber_ = sequenceNumber;
  return true;
}
