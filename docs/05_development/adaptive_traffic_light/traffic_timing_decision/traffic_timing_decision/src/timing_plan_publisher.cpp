#include "timing_plan_publisher.h"

#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <mqueue.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <string_view>

#include "application_logger.h"
#include "common.h"

namespace {

constexpr mode_t kQueuePermissions{0660};
constexpr unsigned int kMessagePriority{0U};

}  // namespace

TimingPlanPublisher::TimingPlanPublisher() = default;

bool TimingPlanPublisher::initialize() {
  if (queueDescriptor_ != static_cast<mqd_t>(-1)) {
    return true;
  }

  mq_attr requestedAttributes{};
  requestedAttributes.mq_maxmsg = traffic_ipc::kTimingPlanQueueMaxMessages;
  requestedAttributes.mq_msgsize = traffic_ipc::kTimingPlanQueueMessageSize;

  queueDescriptor_ =
      mq_open(traffic_ipc::kTimingPlanQueueName,
              O_CREAT | O_WRONLY | O_NONBLOCK | O_CLOEXEC, kQueuePermissions,
              &requestedAttributes);
  if (queueDescriptor_ == static_cast<mqd_t>(-1)) {
    lastError_ = errno;
    traffic_timing_decision::applicationLogger().LogError()
        << "[IPC][PLAN][OPEN] queue=" << traffic_ipc::kTimingPlanQueueName
        << "; errno=" << lastError_
        << "; reason=" << std::string_view{std::strerror(lastError_)};
    return false;
  }

  mq_attr actualAttributes{};
  if (mq_getattr(queueDescriptor_, &actualAttributes) != 0) {
    lastError_ = errno;
    shutdown();
    traffic_timing_decision::applicationLogger().LogError()
        << "[IPC][PLAN][ATTRIBUTES] errno=" << lastError_
        << "; reason=" << std::string_view{std::strerror(lastError_)};
    return false;
  }
  if (actualAttributes.mq_maxmsg !=
          traffic_ipc::kTimingPlanQueueMaxMessages ||
      actualAttributes.mq_msgsize !=
          traffic_ipc::kTimingPlanQueueMessageSize) {
    lastError_ = EMSGSIZE;
    shutdown();
    traffic_timing_decision::applicationLogger().LogError()
        << "[IPC][PLAN][CONTRACT_MISMATCH] queue="
        << traffic_ipc::kTimingPlanQueueName
        << "; expected_maxmsg=" << traffic_ipc::kTimingPlanQueueMaxMessages
        << "; expected_msgsize=" << traffic_ipc::kTimingPlanQueueMessageSize;
    return false;
  }

  publisherInstanceId_ = createPublisherInstanceId();
  nextSequenceNumber_ = 1U;
  pendingMessage_.reset();
  lastError_ = 0;

  traffic_timing_decision::applicationLogger().LogInfo()
      << "[IPC][PLAN][OPEN] queue=" << traffic_ipc::kTimingPlanQueueName
      << "; mode=nonblocking_publisher"
      << "; publisher_instance_id=" << publisherInstanceId_;
  return true;
}

void TimingPlanPublisher::shutdown() {
  if (queueDescriptor_ != static_cast<mqd_t>(-1)) {
    (void)mq_close(queueDescriptor_);
    queueDescriptor_ = static_cast<mqd_t>(-1);
  }
}

bool TimingPlanPublisher::publishTimingPlan(const TimingPlan& plan) {
  pendingMessage_ = makeMessage(plan);
  lastPublishedPlan = plan;
  return trySendPending();
}

bool TimingPlanPublisher::retryPending() {
  return !pendingMessage_.has_value() || trySendPending();
}

bool TimingPlanPublisher::publishPreviousTimingPlan() {
  if (lastPublishedPlan.planId == std::uint64_t{}) {
    return false;
  }
  return publishTimingPlan(lastPublishedPlan);
}

bool TimingPlanPublisher::trySendPending() {
  if (!pendingMessage_.has_value()) {
    return true;
  }
  if (queueDescriptor_ == static_cast<mqd_t>(-1)) {
    lastError_ = EBADF;
    return false;
  }

  const auto& message = *pendingMessage_;
  if (mq_send(queueDescriptor_, reinterpret_cast<const char*>(&message),
              sizeof(message), kMessagePriority) == 0) {
    lastPublishedPlan.planId = message.planId;
    lastPublishTimestampNs = common::monotonicNanoseconds();
    pendingMessage_.reset();
    lastError_ = 0;
    return true;
  }

  lastError_ = errno;
  if (lastError_ == EAGAIN) {
    traffic_timing_decision::applicationLogger().LogWarn()
        << "[IPC][PLAN][DEFERRED] plan_id=" << message.planId
        << "; sequence=" << message.sequenceNumber
        << "; reason=queue_full; retry=next_cycle";
    return true;
  }

  traffic_timing_decision::applicationLogger().LogError()
      << "[IPC][PLAN][SEND] errno=" << lastError_
      << "; reason=" << std::string_view{std::strerror(lastError_)}
      << "; plan_id=" << message.planId
      << "; sequence=" << message.sequenceNumber;
  return false;
}

traffic_ipc::TimingPlanMessageV1 TimingPlanPublisher::makeMessage(
    const TimingPlan& plan) noexcept {
  if (nextSequenceNumber_ == 0U) {
    publisherInstanceId_ = createPublisherInstanceId();
    nextSequenceNumber_ = 1U;
  }

  traffic_ipc::TimingPlanMessageV1 message{};
  message.publisherInstanceId = publisherInstanceId_;
  message.sequenceNumber = nextSequenceNumber_++;
  message.planId = plan.planId;
  message.generationTimestampNs = plan.generationTimestampNs;
  message.greenNorthSouthMs = plan.greenNorthSouthMs;
  message.greenEastWestMs = plan.greenEastWestMs;
  message.yellowMs = plan.yellowMs;
  message.allRedMs = plan.allRedMs;
  message.cycleLengthMs = plan.cycleLengthMs;
  message.emergencyNorthSouth = plan.emergencyNorthSouth ? 1U : 0U;
  message.emergencyEastWest = plan.emergencyEastWest ? 1U : 0U;
  return message;
}

std::uint64_t TimingPlanPublisher::createPublisherInstanceId() noexcept {
  std::uint64_t instanceId{0U};
  ssize_t bytesRead{};
  do {
    bytesRead = getrandom(&instanceId, sizeof(instanceId), GRND_NONBLOCK);
  } while (bytesRead < 0 && errno == EINTR);

  if (bytesRead == static_cast<ssize_t>(sizeof(instanceId)) &&
      instanceId != 0U) {
    return instanceId;
  }

  timespec realtime{};
  timespec monotonic{};
  (void)clock_gettime(CLOCK_REALTIME, &realtime);
  (void)clock_gettime(CLOCK_MONOTONIC, &monotonic);
  instanceId =
      static_cast<std::uint64_t>(realtime.tv_sec) ^
      (static_cast<std::uint64_t>(realtime.tv_nsec) << 32U) ^
      static_cast<std::uint64_t>(monotonic.tv_nsec) ^
      (static_cast<std::uint64_t>(getpid()) << 16U);
  return instanceId == 0U ? 1U : instanceId;
}
