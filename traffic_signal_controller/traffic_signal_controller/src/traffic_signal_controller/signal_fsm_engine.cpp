#include "traffic_signal_controller/signal_fsm_engine.h"

#include <cerrno>
#include <cstdint>
#include <ctime>

namespace {

constexpr std::uint64_t kNanosecondsPerMillisecond{1'000'000ULL};

constexpr std::uint64_t kNanosecondsPerSecond{1'000'000'000ULL};

}  // namespace

SignalFSMEngine::SignalFSMEngine(PlanSyncChannel& syncChannel)
    : syncChannel_{syncChannel} {
  reset();
}

SignalDisplay SignalFSMEngine::processTick() {
  if (!deadlineInitialized_) {
    initializeDeadline();
  }

  addMilliseconds(nextDeadline_, TIMER_INTERVAL_MS);

  const PhaseId processedPhase = currentPhaseId();

  syncChannel_.SetCurrentPhase(processedPhase);

  if (processedPhase == PhaseId::NS_GREEN ||
      processedPhase == PhaseId::EW_GREEN) {
    processGreenPhase(nextDeadline_);
  } else {
    processNonInterruptiblePhase(nextDeadline_);

    decrementRemainingTime();
  }

  const SignalDisplay display{processedPhase, remainingTimeMs_};

  if (remainingTimeMs_ == 0U) {
    /*
     * Normal plan mới được lấy tại cuối ALL_RED,
     * ngay trước khi chuyển sang GREEN kế tiếp.
     */
    if (processedPhase == PhaseId::ALL_RED) {
      loadPendingPlan();
    }
    advancePhase();
  }

  return display;
}

void SignalFSMEngine::reset() noexcept {
  currentPlan_ = MakeDefaultPlan();

  currentPhaseIndex_ = 0U;

  remainingTimeMs_ = currentPlan_.phases[currentPhaseIndex_].durationMs;

  hasActivePlan_ = true;

  nextDeadline_ = timespec{};
  deadlineInitialized_ = false;

  syncChannel_.SetCurrentPhase(currentPhaseId());
}

bool SignalFSMEngine::loadPendingPlan() {
  PlanData pendingPlan{};

  if (!syncChannel_.ConsumePendingPlan(pendingPlan)) {
    return false;
  }

  if (pendingPlan.phaseCount == 0U || pendingPlan.phaseCount > MAX_PHASES) {
    return false;
  }

  /*
   * Nếu chưa có active plan thì bắt đầu từ phase đầu.
   */
  if (!hasActivePlan_) {
    currentPlan_ = pendingPlan;
    currentPhaseIndex_ = 0U;

    remainingTimeMs_ = currentPlan_.phases[currentPhaseIndex_].durationMs;

    hasActivePlan_ = true;

    return true;
  }

  /*
   * Khi đang ở ALL_RED, áp dụng plan mới ngay.
   *
   * Sau ALL_RED hiện tại, advancePhase() sẽ dùng
   * phase tiếp theo của plan mới.
   */
  currentPlan_ = pendingPlan;

  /*
   * Giữ currentPhaseIndex nếu index vẫn hợp lệ.
   * Nếu plan mới có ít phase hơn thì bắt đầu lại.
   */
  if (currentPhaseIndex_ >= currentPlan_.phaseCount) {
    currentPhaseIndex_ = 0U;
  }

  return true;
}

void SignalFSMEngine::advancePhase() {
  if (!hasActivePlan_ || currentPlan_.phaseCount == 0U) {
    hasActivePlan_ = false;
    currentPhaseIndex_ = 0U;
    remainingTimeMs_ = 0U;
    return;
  }

  ++currentPhaseIndex_;

  if (currentPhaseIndex_ >= currentPlan_.phaseCount) {
    currentPhaseIndex_ = 0U;
  }

  remainingTimeMs_ = currentPlan_.phases[currentPhaseIndex_].durationMs;

  syncChannel_.SetCurrentPhase(currentPhaseId());
}

void SignalFSMEngine::processGreenPhase(const timespec& absoluteDeadline) {
  PlanData emergencyPlan{};

  const PlanSyncChannel::WaitResult result =
      syncChannel_.WaitForEmergencyUntil(absoluteDeadline, emergencyPlan);

  switch (result) {
    case PlanSyncChannel::WaitResult::EMERGENCY_AVAILABLE: {
      const bool accepted = evaluateEmergencyPlan(emergencyPlan);

      if (accepted) {
        applyEmergencyPlan(emergencyPlan);
      } else if (remainingTimeMs_ > 0U) {
        processGreenPhase(absoluteDeadline);
      }
      break;
    }

    case PlanSyncChannel::WaitResult::TIMEOUT:
      decrementRemainingTime();
      break;

    case PlanSyncChannel::WaitResult::ERROR:
      /*
       * Có thể là shutdown hoặc lỗi pthread.
       * Không thay đổi timing state.
       */
      break;
  }
}

void SignalFSMEngine::processNonInterruptiblePhase(
    const timespec& absoluteDeadline) {
  int result{};

  do {
    result = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &absoluteDeadline,
                             nullptr);
  } while (result == EINTR);
}

bool SignalFSMEngine::evaluateEmergencyPlan(
    const PlanData& emergencyPlan) const {
  if (!hasActivePlan_) {
    return false;
  }

  // Phải có đúng một hướng emergency.
  if (emergencyPlan.isEmergencyNS == emergencyPlan.isEmergencyEW) {
    return false;
  }

  constexpr std::uint32_t kThresholdMs{5'000U};
  constexpr std::uint32_t kMinEmergencyMs{10'000U};

  /*
   * Chấp nhận khi:
   *
   * 5 giây < thời gian còn lại < 10 giây.
   */
  const bool timingWindowValid =
      remainingTimeMs_ > kThresholdMs &&
      remainingTimeMs_ < kMinEmergencyMs;

  if (!timingWindowValid) {
    return false;
  }

  const PhaseId phaseId = currentPhaseId();

  // Emergency phải cùng hướng với GREEN hiện tại.
  if (phaseId == PhaseId::NS_GREEN) {
    return emergencyPlan.isEmergencyNS;
  }

  if (phaseId == PhaseId::EW_GREEN) {
    return emergencyPlan.isEmergencyEW;
  }

  return false;
}


void SignalFSMEngine::applyEmergencyPlan(
    const PlanData& emergencyPlan) {
  constexpr std::uint32_t kEmergencyGreenDurationMs{20'000U};

  const PhaseId phaseId = currentPhaseId();

  if (phaseId == PhaseId::NS_GREEN &&
      emergencyPlan.isEmergencyNS) {
    remainingTimeMs_ = kEmergencyGreenDurationMs;
    return;
  }

  if (phaseId == PhaseId::EW_GREEN &&
      emergencyPlan.isEmergencyEW) {
    remainingTimeMs_ = kEmergencyGreenDurationMs;
  }
}

void SignalFSMEngine::decrementRemainingTime() noexcept {
  if (remainingTimeMs_ <= TIMER_INTERVAL_MS) {
    remainingTimeMs_ = 0U;
    return;
  }

  remainingTimeMs_ -= TIMER_INTERVAL_MS;
}

void SignalFSMEngine::initializeDeadline() {
  if (clock_gettime(CLOCK_MONOTONIC, &nextDeadline_) != 0) {
    nextDeadline_ = timespec{};
  }

  deadlineInitialized_ = true;
}

void SignalFSMEngine::addMilliseconds(
    timespec& timestamp, const std::uint32_t milliseconds) noexcept {
  const std::uint64_t additionalNanoseconds =
      static_cast<std::uint64_t>(milliseconds) * kNanosecondsPerMillisecond;

  const std::uint64_t totalNanoseconds =
      static_cast<std::uint64_t>(timestamp.tv_nsec) + additionalNanoseconds;

  timestamp.tv_sec +=
      static_cast<time_t>(totalNanoseconds / kNanosecondsPerSecond);

  timestamp.tv_nsec =
      static_cast<long>(totalNanoseconds % kNanosecondsPerSecond);
}

PhaseId SignalFSMEngine::currentPhaseId() const noexcept {
  if (!hasActivePlan_ || currentPlan_.phaseCount == 0U ||
      currentPhaseIndex_ >= currentPlan_.phaseCount) {
    return PhaseId::ALL_RED;
  }

  return currentPlan_.phases[currentPhaseIndex_].phaseId;
}