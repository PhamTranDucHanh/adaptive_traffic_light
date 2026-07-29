#include "traffic_signal_controller/plan_receiver.h"

#include <ctime>
#include <limits>

#include "common/logging_contexts.h"
#include "score/mw/log/logger.h"
namespace {

constexpr std::uint32_t kMinGreenDurationMs{1000U};
constexpr std::uint32_t kMaxGreenDurationMs{500000U};

constexpr std::uint32_t kMinYellowDurationMs{1000U};
constexpr std::uint32_t kMaxYellowDurationMs{10000U};

constexpr std::uint32_t kMinAllRedDurationMs{500U};
constexpr std::uint32_t kMaxAllRedDurationMs{10000U};

score::mw::log::Logger& Logger() {
  static auto& logger =
      score::mw::log::CreateLogger(ctrl::logging::kCtxPlan, "Plan Receiver");
  return logger;
}

}  // namespace

PlanReceiver::PlanReceiver(PlanSyncChannel& syncChannel)
    : syncChannel_{syncChannel} {}

bool PlanReceiver::ReceivePlan(const TimingPlan& plan) {
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

  const std::uint64_t receivedTimestampNs = GetMonotonicTimestampNs();
  const PlanData translatedPlan = TranslatePlan(plan, receivedTimestampNs);

  const bool result = syncChannel_.PublishPlan(translatedPlan);

  Logger().LogInfo() << "event=PLAN_PUBLISHED"
                     << ", plan_id=" << plan.planId
                     << ", result=" << (result ? "SUCCESS" : "FAILED");

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

PlanData PlanReceiver::TranslatePlan(
    const TimingPlan& plan, const std::uint64_t receivedTimestampNs) const {
  PlanData output{};

  output.sourcePlanId = plan.planId;
  output.isEmergencyNS = plan.emergencyNorthSouth;
  output.isEmergencyEW = plan.emergencyEastWest;
  output.receivedAt = receivedTimestampNs;

  output.phases[0] = Phase{PhaseId::NS_GREEN, plan.greenNorthSouthMs};

  output.phases[1] = Phase{PhaseId::YELLOW, plan.yellowMs};

  output.phases[2] = Phase{PhaseId::ALL_RED, plan.allRedMs};

  output.phases[3] = Phase{PhaseId::EW_GREEN, plan.greenEastWestMs};

  output.phases[4] = Phase{PhaseId::YELLOW, plan.yellowMs};

  output.phases[5] = Phase{PhaseId::ALL_RED, plan.allRedMs};

  output.phaseCount = 6U;

  const std::uint64_t calculatedCycleLengthMs =
      static_cast<std::uint64_t>(plan.greenNorthSouthMs) +
      static_cast<std::uint64_t>(plan.greenEastWestMs) +
      2ULL * static_cast<std::uint64_t>(plan.yellowMs) +
      2ULL * static_cast<std::uint64_t>(plan.allRedMs);

  output.totalCycleMs = static_cast<std::uint32_t>(calculatedCycleLengthMs);
  return output;
}

std::uint64_t PlanReceiver::GetMonotonicTimestampNs() {
  timespec timestamp{};
  if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0) {
    return 0U;
  }

  constexpr std::uint64_t kNanosecondsPerSecond{1'000'000'000ULL};

  return static_cast<std::uint64_t>(timestamp.tv_sec) * kNanosecondsPerSecond +
         static_cast<std::uint64_t>(timestamp.tv_nsec);
}