#include "timing_plan_publisher.h"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>

#include "application_logger.h"
#include "common.h"

namespace {

bool HasEmergency(const TimingPlan& plan) noexcept {
  return plan.emergencyNorthSouth || plan.emergencyEastWest;
}

bool HasSameSignalOutput(const TimingPlan& lhs,
                         const TimingPlan& rhs) noexcept {
  return lhs.greenNorthSouthMs == rhs.greenNorthSouthMs &&
         lhs.greenEastWestMs == rhs.greenEastWestMs &&
         lhs.yellowMs == rhs.yellowMs && lhs.allRedMs == rhs.allRedMs &&
         lhs.cycleLengthMs == rhs.cycleLengthMs &&
         lhs.emergencyNorthSouth == rhs.emergencyNorthSouth &&
         lhs.emergencyEastWest == rhs.emergencyEastWest;
}

}  // namespace

//
// Constructor
//
TimingPlanPublisher::TimingPlanPublisher() = default;

TimingPlanPublisher::~TimingPlanPublisher() { shutdown(); }

bool TimingPlanPublisher::initialize() {
  if (descriptor_ != static_cast<mqd_t>(-1)) {
    return true;
  }

  mq_attr attributes{};
  attributes.mq_flags = 0L;
  attributes.mq_maxmsg = traffic_ipc::kTimingPlanQueueMaxMessages;
  attributes.mq_msgsize = traffic_ipc::kTimingPlanQueueMessageSize;
  attributes.mq_curmsgs = 0L;

  descriptor_ = mq_open(
      traffic_ipc::kTimingPlanQueueName,
      O_CREAT | O_WRONLY | O_NONBLOCK | O_CLOEXEC,
      static_cast<mode_t>(0660), &attributes);
  if (descriptor_ == static_cast<mqd_t>(-1)) {
    lastError_ = errno;
    traffic_timing_decision::ipcLogger().LogError()
        << "[IPC][PLAN][OPEN] queue="
        << traffic_ipc::kTimingPlanQueueName << "; errno=" << lastError_;
    return false;
  }

  if (!validateQueueContract()) {
    traffic_timing_decision::ipcLogger().LogError()
        << "[IPC][PLAN][CONTRACT_MISMATCH] queue="
        << traffic_ipc::kTimingPlanQueueName
        << "; expected_maxmsg=" << traffic_ipc::kTimingPlanQueueMaxMessages
        << "; expected_msgsize=" << traffic_ipc::kTimingPlanQueueMessageSize
        << "; errno=" << lastError_;
    shutdown();
    return false;
  }

  const std::uint64_t processId = static_cast<std::uint64_t>(getpid());
  publisherInstanceId_ = common::monotonicNanoseconds() ^
                         (processId << 32U) ^ processId;
  if (publisherInstanceId_ == 0U) {
    publisherInstanceId_ = processId != 0U ? processId : 1U;
  }
  sequenceNumber_ = 0U;
  pendingLatest_.reset();
  pendingLatestPlan_.reset();
  lastPublishedPlan_ = TimingPlan{};
  lastPublishTimestampNs_ = std::uint64_t{};
  lastPlanSuppressed_ = false;
  lastError_ = 0;

  traffic_timing_decision::ipcLogger().LogInfo()
      << "[IPC][PLAN][OPEN] queue=" << traffic_ipc::kTimingPlanQueueName
      << "; mode=nonblocking_write_only"
      << "; maxmsg=" << traffic_ipc::kTimingPlanQueueMaxMessages
      << "; msgsize=" << traffic_ipc::kTimingPlanQueueMessageSize
      << "; publisher_instance_id=" << publisherInstanceId_;
  return true;
}

void TimingPlanPublisher::shutdown() {
  if (descriptor_ != static_cast<mqd_t>(-1)) {
    (void)mq_close(descriptor_);
    descriptor_ = static_cast<mqd_t>(-1);
  }
  pendingLatest_.reset();
  pendingLatestPlan_.reset();
}

//
// Publish
//
bool TimingPlanPublisher::publishTimingPlan(const TimingPlan& plan) {
  lastPlanSuppressed_ = false;

  // Emergency plans always take the normal immediate publish path, even when
  // their durations and flags match the previously delivered plan.
  if (!HasEmergency(plan)) {
    if (pendingLatestPlan_.has_value() &&
        HasSameSignalOutput(plan, *pendingLatestPlan_)) {
      lastPlanSuppressed_ = true;
      traffic_timing_decision::ipcLogger().LogDebug()
          << "[IPC][PLAN][UNCHANGED] plan_id=" << plan.planId
          << "; compared_with=pending_latest; action=retry_pending_only";
      return trySendPending();
    }

    if (lastPublishedPlan_.planId != std::uint64_t{} &&
        HasSameSignalOutput(plan, lastPublishedPlan_)) {
      // A newer pending value is obsolete when the newest decision has already
      // returned to the signal output currently held by the controller.
      pendingLatest_.reset();
      pendingLatestPlan_.reset();
      lastPlanSuppressed_ = true;
      traffic_timing_decision::ipcLogger().LogDebug()
          << "[IPC][PLAN][UNCHANGED] plan_id=" << plan.planId
          << "; compared_with=last_published; action=skip_send";
      return true;
    }
  }

  pendingLatest_ = makeMessage(plan);
  pendingLatestPlan_ = plan;
  return trySendPending();
}

bool TimingPlanPublisher::retryPendingTimingPlan() {
  return trySendPending();
}

bool TimingPlanPublisher::publishPreviousTimingPlan() {
  if (lastPublishedPlan_.planId == std::uint64_t{}) {
    return false;
  }
  return publishTimingPlan(lastPublishedPlan_);
}

bool TimingPlanPublisher::wasLastPlanSuppressed() const noexcept {
  return lastPlanSuppressed_;
}

bool TimingPlanPublisher::validateQueueContract() noexcept {
  mq_attr attributes{};
  if (mq_getattr(descriptor_, &attributes) != 0) {
    lastError_ = errno;
    return false;
  }
  if (attributes.mq_maxmsg != traffic_ipc::kTimingPlanQueueMaxMessages ||
      attributes.mq_msgsize != traffic_ipc::kTimingPlanQueueMessageSize) {
    lastError_ = EMSGSIZE;
    return false;
  }
  lastError_ = 0;
  return true;
}

traffic_ipc::TimingPlanMessageV1 TimingPlanPublisher::makeMessage(
    const TimingPlan& plan) noexcept {
  traffic_ipc::TimingPlanMessageV1 message{};
  message.publisherInstanceId = publisherInstanceId_;
  message.sequenceNumber = nextSequenceNumber();
  message.planId = plan.planId;
  message.generationTimestampNs = plan.generationTimestampNs;
  message.perceptionPublishTimestampUs = plan.perceptionPublishTimestampUs;
  message.greenNorthSouthMs = plan.greenNorthSouthMs;
  message.greenEastWestMs = plan.greenEastWestMs;
  message.yellowMs = plan.yellowMs;
  message.allRedMs = plan.allRedMs;
  message.cycleLengthMs = plan.cycleLengthMs;
  message.emergencyNorthSouth = plan.emergencyNorthSouth ? 1U : 0U;
  message.emergencyEastWest = plan.emergencyEastWest ? 1U : 0U;
  return message;
}

bool TimingPlanPublisher::trySendPending() noexcept {
  if (!pendingLatest_.has_value()) {
    return true;
  }

  if (descriptor_ == static_cast<mqd_t>(-1)) {
    lastError_ = EBADF;
    traffic_timing_decision::ipcLogger().LogError()
        << "[IPC][PLAN][SEND] errno=" << lastError_
        << "; plan_id=" << pendingLatest_->planId
        << "; sequence=" << pendingLatest_->sequenceNumber;
    return false;
  }

  const traffic_ipc::TimingPlanMessageV1 candidate = *pendingLatest_;
  if (mq_send(descriptor_, reinterpret_cast<const char*>(&candidate),
              sizeof(candidate), 0U) != 0) {
    lastError_ = errno;
    if (lastError_ == EAGAIN || lastError_ == EINTR) {
      traffic_timing_decision::ipcLogger().LogWarn()
          << "[IPC][PLAN][DEFERRED] plan_id=" << candidate.planId
          << "; sequence=" << candidate.sequenceNumber
          << "; errno=" << lastError_
          << "; reason=queue_unavailable; retry=next_cycle";
      return true;
    }

    traffic_timing_decision::ipcLogger().LogError()
        << "[IPC][PLAN][SEND] errno=" << lastError_
        << "; plan_id=" << candidate.planId
        << "; sequence=" << candidate.sequenceNumber;
    return false;
  }

  if (pendingLatestPlan_.has_value()) {
    lastPublishedPlan_ = *pendingLatestPlan_;
    lastPublishTimestampNs_ = common::monotonicNanoseconds();
  }
  pendingLatest_.reset();
  pendingLatestPlan_.reset();
  lastError_ = 0;
  traffic_timing_decision::ipcLogger().LogInfo()
      << "[IPC][PLAN][SENT] plan_id=" << candidate.planId
      << "; publisher_instance_id=" << candidate.publisherInstanceId
      << "; sequence=" << candidate.sequenceNumber
      << "; perception_publish_timestamp_us="
      << candidate.perceptionPublishTimestampUs;
  return true;
}

std::uint64_t TimingPlanPublisher::nextSequenceNumber() noexcept {
  if (sequenceNumber_ == std::numeric_limits<std::uint64_t>::max()) {
    sequenceNumber_ = 1U;
  } else {
    ++sequenceNumber_;
  }
  return sequenceNumber_;
}
