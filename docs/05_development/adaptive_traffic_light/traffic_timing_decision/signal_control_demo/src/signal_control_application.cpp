#include "signal_control_application.h"

#include <cstdlib>
#include <cstring>
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

SignalControlApplication::SignalControlApplication()
    : consumer_{traffic_ipc::kTimingPlanQueueName,
                traffic_ipc::kTimingPlanLockName} {
  lastValidPlan_.greenNorthSouthMs = 30000U;
  lastValidPlan_.greenEastWestMs = 30000U;
  lastValidPlan_.yellowMs = 3000U;
  lastValidPlan_.allRedMs = 1000U;
  lastValidPlan_.cycleLengthMs = 68000U;
}

std::int32_t SignalControlApplication::Initialize(
    const score::mw::lifecycle::ApplicationContext& context) {
  (void)context;

  const auto queueStatus = consumer_.open();
  if (queueStatus != traffic_ipc::QueueStatus::kSuccess) {
    applicationLogger().LogError()
        << "[INIT][IPC] queue=" << traffic_ipc::kTimingPlanQueueName
        << "; status="
        << std::string_view{traffic_ipc::queueStatusName(queueStatus)}
        << "; errno=" << consumer_.lastError();
    return EXIT_FAILURE;
  }
  if (!periodicWait_.valid()) {
    applicationLogger().LogError()
        << "[INIT][PERIODIC] condition variable initialization failed";
    consumer_.close();
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
    const auto receiveStatus = consumer_.receiveLatest(plan);
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
  if (plan.planId == 0U || plan.planId <= lastValidPlan_.planId ||
      plan.generationTimestampNs == 0U) {
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

void SignalControlApplication::shutdown() {
  if (!initialized_) {
    return;
  }
  applicationLogger().LogInfo()
      << "[STOP] output_state=all_red_safe; last_plan_id="
      << lastValidPlan_.planId;
  consumer_.close();
  initialized_ = false;
}

}  // namespace signal_control_demo
