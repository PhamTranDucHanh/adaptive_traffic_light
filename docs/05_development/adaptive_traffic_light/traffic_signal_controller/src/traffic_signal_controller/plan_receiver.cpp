#include "traffic_signal_controller/plan_receiver.h"

#include <ctime>
#include <limits>

#include "common/logging_contexts.h"
#include "score/mw/log/logger.h"
namespace {

score::mw::log::Logger& Logger() {
  static auto& logger =
      score::mw::log::CreateLogger(ctrl::logging::kCtxPlan, "Plan Receiver");
  return logger;
}

}  // namespace

PlanReceiver::PlanReceiver(PlanSyncChannel& syncChannel)
    : syncChannel_{syncChannel} {}

bool PlanReceiver::ReceivePlan(const TimingPlan& plan) {
  timespec receiveTime{};
  const bool receiveTimestampValid =
      clock_gettime(CLOCK_MONOTONIC, &receiveTime) == 0;
  const std::uint64_t receiveTimestampNs =
      receiveTimestampValid
          ? (static_cast<std::uint64_t>(receiveTime.tv_sec) *
             kNanosecondsPerSecond) +
                static_cast<std::uint64_t>(receiveTime.tv_nsec)
          : 0U;

  Logger().LogInfo() << "event=PLAN_RECEIVED"
                     << ", plan_id=" << plan.planId
                     << ", emergency_ns=" << plan.emergencyNorthSouth
                     << ", emergency_ew=" << plan.emergencyEastWest;

  if (!ValidatePlan(plan)) {
    Logger().LogWarn() << "event=PLAN_REJECTED"
                       << ", plan_id=" << plan.planId
                       << ", reason=INVALID_PLAN";
    return false;
  }

  Logger().LogInfo() << "event=PLAN_VALIDATED"
                     << ", plan_id=" << plan.planId;

  PlanData translatedPlan = TranslatePlan(plan);
  translatedPlan.controllerReceiveTimestampNs = receiveTimestampNs;

  const bool result = syncChannel_.PublishPlan(translatedPlan);

  Logger().LogInfo() << "event=PLAN_PUBLISHED"
                     << ", plan_id=" << plan.planId
                     << ", result=" << (result ? "SUCCESS" : "FAILED");

  if (result && receiveTimestampValid && plan.generationTimestampNs > 0U) {
    const std::uint64_t decisionRxNs = plan.generationTimestampNs;

    if (receiveTimestampNs >= decisionRxNs) {
      Logger().LogInfo()
          << "event=TIMING_DECISION_RECEIVE_TO_CONTROLLER_RECEIVE"
          << ", plan_id=" << plan.planId
          << ", timing_receive_timestamp_ns=" << decisionRxNs
          << ", controller_receive_timestamp_ns=" << receiveTimestampNs
          << ", latency_ns=" << (receiveTimestampNs - decisionRxNs);
    }
  }

  constexpr std::uint64_t kNanosecondsPerMicrosecond{1'000U};
  if (result && receiveTimestampValid &&
      plan.perceptionPublishTimestampUs > 0U &&
      plan.perceptionPublishTimestampUs <=
          (std::numeric_limits<std::uint64_t>::max() /
           kNanosecondsPerMicrosecond)) {
    const std::uint64_t perceptionTxNs =
        plan.perceptionPublishTimestampUs * kNanosecondsPerMicrosecond;

    if (receiveTimestampNs >= perceptionTxNs) {
      Logger().LogInfo() << "event=PERCEPTION_PUBLISH_TO_CONTROLLER_RECEIVE"
                         << ", plan_id=" << plan.planId
                         << ", perception_publish_timestamp_ns="
                         << perceptionTxNs
                         << ", controller_receive_timestamp_ns="
                         << receiveTimestampNs << ", latency_ns="
                         << (receiveTimestampNs - perceptionTxNs);
    }
  }

  return result;
}

bool PlanReceiver::ValidatePlan(const TimingPlan& plan) const {
  if (plan.planId == 0U) {
    return false;
  }

  // Emergency cả hai hướng
  if (plan.emergencyNorthSouth && plan.emergencyEastWest) {
    return false;
  }

  if (plan.greenNorthSouthMs < kMinGreenDurationMs ||
      plan.greenNorthSouthMs > kMaxGreenDurationMs) {
    return false;
  }

  if (plan.greenEastWestMs < kMinGreenDurationMs ||
      plan.greenEastWestMs > kMaxGreenDurationMs) {
    return false;
  }

  if (plan.yellowMs < kMinYellowDurationMs ||
      plan.yellowMs > kMaxYellowDurationMs) {
    return false;
  }

  if (plan.allRedMs < kMinAllRedDurationMs ||
      plan.allRedMs > kMaxAllRedDurationMs) {
    return false;
  }

  const std::uint64_t calculatedCycleLengthMs =
      static_cast<std::uint64_t>(plan.greenNorthSouthMs) +
      static_cast<std::uint64_t>(plan.greenEastWestMs) +
      2ULL * static_cast<std::uint64_t>(plan.yellowMs) +
      2ULL * static_cast<std::uint64_t>(plan.allRedMs);

  if (calculatedCycleLengthMs > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }

  /*
   * cycleLengthMs == 0:
   * Cho phép PlanReceiver tự tính.
   *
   * cycleLengthMs != 0:
   * Giá trị bên gửi phải khớp với tổng duration.
   */
  if (plan.cycleLengthMs != 0U &&
      plan.cycleLengthMs != calculatedCycleLengthMs) {
    return false;
  }

  return true;
}

PlanData PlanReceiver::TranslatePlan(const TimingPlan& plan) const {
  PlanData output{};

  output.sourcePlanId = plan.planId;
  output.isEmergencyNS = plan.emergencyNorthSouth;
  output.isEmergencyEW = plan.emergencyEastWest;

  output.phases[0] = Phase{PhaseId::NS_GREEN, plan.greenNorthSouthMs};

  output.phases[1] = Phase{PhaseId::YELLOW, plan.yellowMs};

  output.phases[2] = Phase{PhaseId::ALL_RED, plan.allRedMs};

  output.phases[3] = Phase{PhaseId::EW_GREEN, plan.greenEastWestMs};

  output.phases[4] = Phase{PhaseId::YELLOW, plan.yellowMs};

  output.phases[5] = Phase{PhaseId::ALL_RED, plan.allRedMs};

  output.phaseCount = 6U;

  return output;
}
