#include "signal_control_application.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mqueue.h>
#include <score/stop_token.hpp>
#include <string_view>

#include "common.h"
#include "signal_control_logger.h"

namespace {

constexpr std::uint32_t kPeriodMs = 3000U;
constexpr std::uint32_t kMinimumGreenMs = 10000U;
constexpr std::uint32_t kMaximumGreenMs = 60000U;
constexpr std::uint32_t kMaximumCycleMs = 100000U;
constexpr std::uint64_t kMaximumPlanAgeNs = 10000000000ULL;
constexpr std::uint32_t kMaximumConsecutiveMisses = 3U;

}  // namespace

namespace signal_control_demo {

SignalControlApplication::SignalControlApplication() {
  lastValidPlan_.greenNorthSouthMs = 30000U;
  lastValidPlan_.greenEastWestMs = 30000U;
  lastValidPlan_.yellowMs = 3000U;
  lastValidPlan_.allRedMs = 1000U;
  lastValidPlan_.cycleLengthMs = 68000U;
}

std::int32_t SignalControlApplication::Initialize(
    const score::mw::lifecycle::ApplicationContext& context) {
  (void)context;

  queueDescriptor_ = mq_open(traffic_ipc::kTimingPlanQueueName,
                             O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (queueDescriptor_ == static_cast<mqd_t>(-1)) {
    applicationLogger().LogError()
        << "[INIT][IPC] queue=" << traffic_ipc::kTimingPlanQueueName
        << "; errno=" << errno;
    return EXIT_FAILURE;
  }

  mq_attr attributes{};
  if (mq_getattr(queueDescriptor_, &attributes) != 0 ||
      attributes.mq_maxmsg != traffic_ipc::kTimingPlanQueueMaxMessages ||
      attributes.mq_msgsize != traffic_ipc::kTimingPlanQueueMessageSize) {
    applicationLogger().LogError()
        << "[INIT][IPC] queue contract mismatch; expected_maxmsg="
        << traffic_ipc::kTimingPlanQueueMaxMessages
        << "; expected_msgsize=" << traffic_ipc::kTimingPlanQueueMessageSize;
    (void)mq_close(queueDescriptor_);
    queueDescriptor_ = static_cast<mqd_t>(-1);
    return EXIT_FAILURE;
  }

  if (!periodicWait_.valid()) {
    applicationLogger().LogError()
        << "[INIT][PERIODIC] condition variable initialization failed";
    (void)mq_close(queueDescriptor_);
    queueDescriptor_ = static_cast<mqd_t>(-1);
    return EXIT_FAILURE;
  }

  consecutiveMisses_ = 0U;
  initialized_ = true;
  applicationLogger().LogInfo()
      << "[INIT] ready; output_state=all_red_safe; period_ms=" << kPeriodMs
      << "; queue=" << traffic_ipc::kTimingPlanQueueName
      << "; lifecycle_profile=Reporting";
  return EXIT_SUCCESS;
}

std::int32_t SignalControlApplication::Run(
    const score::cpp::stop_token& stopToken) {
  if (!initialized_) {
    return EXIT_FAILURE;
  }

  timespec nextRelease{};
  if (!common::monotonicNow(nextRelease)) {
    shutdown();
    return EXIT_FAILURE;
  }
  common::addMilliseconds(nextRelease, kPeriodMs);

  std::int32_t exitCode{EXIT_SUCCESS};
  score::cpp::stop_callback stopWake{
      stopToken, [this]() noexcept { periodicWait_.requestStop(); }};
  applicationLogger().LogInfo()
      << "[RUN] periodic consumer started; clock=CLOCK_MONOTONIC; "
         "wait=pthread_cond_timedwait; deadline=absolute";
  while (!stopToken.stop_requested()) {
    const int sleepResult = periodicWait_.waitUntil(nextRelease);
    if (sleepResult == ECANCELED || stopToken.stop_requested()) {
      break;
    }
    if (sleepResult != 0) {
      applicationLogger().LogError()
          << "[RUN] periodic wait failed: "
          << std::string_view{std::strerror(sleepResult)};
      exitCode = EXIT_FAILURE;
      break;
    }

    traffic_ipc::TimingPlan plan{};
    const auto receiveStatus = receiveLatestPlan(plan);
    if (receiveStatus == traffic_ipc::QueueStatus::kSuccess &&
        validatePlan(plan)) {
      lastValidPlan_ = plan;
      consecutiveMisses_ = 0U;
#ifdef SIGNAL_CONTROLLER_CONSUMED
      applicationLogger().LogInfo()
          << "[SIGNAL][PLAN][CONSUMED] plan_id=" << plan.planId
          << "; ns_green_ms=" << plan.greenNorthSouthMs
          << "; ew_green_ms=" << plan.greenEastWestMs
          << "; cycle_ms=" << plan.cycleLengthMs
          << "; emergency_ns=" << plan.emergencyNorthSouth
          << "; emergency_ew=" << plan.emergencyEastWest;
#endif
    } else {
      ++consecutiveMisses_;
      if (receiveStatus == traffic_ipc::QueueStatus::kSuccess) {
        applicationLogger().LogWarn()
            << "[IPC][PLAN][REJECTED] plan_id=" << plan.planId
            << "; action=retain_last_valid_or_safe_plan";
      } else {
        applicationLogger().LogWarn()
            << "[IPC][PLAN][NO_NEW_DATA] status="
            << std::string_view{traffic_ipc::queueStatusName(receiveStatus)}
            << "; consecutive_misses=" << consecutiveMisses_
            << "; active_plan_id=" << lastValidPlan_.planId;
      }
    }

    if (consecutiveMisses_ >= kMaximumConsecutiveMisses) {
      applicationLogger().LogError()
          << "[FSM][PLAN_TIMEOUT] consecutive_misses=" << consecutiveMisses_
          << "; threshold=" << kMaximumConsecutiveMisses
          << "; safe_plan_retained=true";
      exitCode = EXIT_FAILURE;
    }
    if (exitCode != EXIT_SUCCESS) {
      break;
    }

    common::addMilliseconds(nextRelease, kPeriodMs);
    timespec now{};
    if (common::monotonicNow(now)) {
      const auto skipped = common::advancePastNow(nextRelease, kPeriodMs, now);
      if (skipped > 0U) {
        applicationLogger().LogWarn()
            << "[RUN][OVERRUN] skipped_releases=" << skipped;
      }
    }
  }

  shutdown();
  return exitCode;
}

bool SignalControlApplication::validatePlan(
    const traffic_ipc::TimingPlan& plan) const noexcept {
  if (plan.planId == 0U || plan.generationTimestampNs == 0U) {
    return false;
  }

  const std::uint64_t now = common::monotonicNanoseconds();
  if (plan.generationTimestampNs > now ||
      now > plan.generationTimestampNs + kMaximumPlanAgeNs) {
    return false;
  }

  const bool greenValid = plan.greenNorthSouthMs >= kMinimumGreenMs &&
                          plan.greenNorthSouthMs <= kMaximumGreenMs &&
                          plan.greenEastWestMs >= kMinimumGreenMs &&
                          plan.greenEastWestMs <= kMaximumGreenMs;
  const std::uint64_t expectedCycle =
      static_cast<std::uint64_t>(plan.greenNorthSouthMs) +
      plan.greenEastWestMs +
      2ULL * (static_cast<std::uint64_t>(plan.yellowMs) + plan.allRedMs);
  return greenValid && expectedCycle == plan.cycleLengthMs &&
         expectedCycle <= kMaximumCycleMs;
}

traffic_ipc::QueueStatus SignalControlApplication::receiveLatestPlan(
    traffic_ipc::TimingPlan& plan) noexcept {
  traffic_ipc::TimingPlanMessageV1 newest{};
  bool hasCandidate{false};

  for (;;) {
    traffic_ipc::TimingPlanMessageV1 current{};
    const ssize_t received =
        mq_receive(queueDescriptor_, reinterpret_cast<char*>(&current),
                   sizeof(current), nullptr);
    if (received < 0) {
      if (errno == EAGAIN) {
        break;
      }
      return traffic_ipc::QueueStatus::kSystemError;
    }
    if (received != static_cast<ssize_t>(sizeof(current)) ||
        !traffic_ipc::HasValidTimingPlanEnvelope(current)) {
      continue;
    }
    if (!isNewerTransportMessage(current.publisherInstanceId,
                                 current.sequenceNumber)) {
      continue;
    }
    newest = current;
    hasCandidate = true;
  }

  if (!hasCandidate) {
    return traffic_ipc::QueueStatus::kEmpty;
  }

  plan.planId = newest.planId;
  plan.generationTimestampNs = newest.generationTimestampNs;
  plan.greenNorthSouthMs = newest.greenNorthSouthMs;
  plan.greenEastWestMs = newest.greenEastWestMs;
  plan.yellowMs = newest.yellowMs;
  plan.allRedMs = newest.allRedMs;
  plan.cycleLengthMs = newest.cycleLengthMs;
  plan.emergencyNorthSouth = newest.emergencyNorthSouth == 1U;
  plan.emergencyEastWest = newest.emergencyEastWest == 1U;
  return traffic_ipc::QueueStatus::kSuccess;
}

bool SignalControlApplication::isNewerTransportMessage(
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

void SignalControlApplication::shutdown() {
  if (!initialized_) {
    return;
  }
  applicationLogger().LogInfo()
      << "[STOP] output_state=all_red_safe; last_plan_id="
      << lastValidPlan_.planId;
  if (queueDescriptor_ != static_cast<mqd_t>(-1)) {
    (void)mq_close(queueDescriptor_);
    queueDescriptor_ = static_cast<mqd_t>(-1);
  }
  initialized_ = false;
}

}  // namespace signal_control_demo
