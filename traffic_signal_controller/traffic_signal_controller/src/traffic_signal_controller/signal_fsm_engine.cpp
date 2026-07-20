#include "traffic_signal_controller/signal_fsm_engine.h"

#include <cerrno>
#include <cstdint>
#include <ctime>

namespace {

constexpr std::uint64_t kNanosecondsPerMillisecond{1'000'000ULL};

constexpr std::uint64_t kNanosecondsPerSecond{1'000'000'000ULL};

bool IsGreenPhase(const PhaseId phaseId) {
  return phaseId == PhaseId::NS_GREEN || phaseId == PhaseId::EW_GREEN;
}

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
      if (evaluateEmergencyPlan(emergencyPlan)) {
        applyEmergencyPlan(emergencyPlan);
      }

      /*
       * Không decrement ở đây vì condition variable có thể
       * thức trước khi tick kết thúc.
       *
       * Nếu emergency không được áp dụng, chờ tiếp đến cùng
       * absolute deadline ban đầu.
       */
      if (remainingTimeMs_ > 0U && !evaluateEmergencyPlan(emergencyPlan)) {
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

  if (!emergencyPlan.isEmergencyNS && !emergencyPlan.isEmergencyEW) {
    return false;
  }

  /*
   * Hai hướng cùng emergency là yêu cầu không hợp lệ.
   */
  if (emergencyPlan.isEmergencyNS && emergencyPlan.isEmergencyEW) {
    return false;
  }

  const PhaseId phaseId = currentPhaseId();

  /*
   * Trong NS_GREEN:
   *
   * - Emergency NS: giữ/điều chỉnh GREEN hiện tại.
   * - Emergency EW: kết thúc NS_GREEN để chuyển qua
   *   YELLOW -> ALL_RED -> EW_GREEN.
   */
  if (phaseId == PhaseId::NS_GREEN) {
    return emergencyPlan.isEmergencyNS || emergencyPlan.isEmergencyEW;
  }

  /*
   * Trong EW_GREEN:
   *
   * - Emergency EW: giữ/điều chỉnh GREEN hiện tại.
   * - Emergency NS: kết thúc EW_GREEN để chuyển qua
   *   YELLOW -> ALL_RED -> NS_GREEN.
   */
  if (phaseId == PhaseId::EW_GREEN) {
    return emergencyPlan.isEmergencyNS || emergencyPlan.isEmergencyEW;
  }

  return false;
}

void SignalFSMEngine::applyEmergencyPlan(const PlanData& emergencyPlan) {
  const PhaseId phaseId = currentPhaseId();

  /*
   * Thay timing data bằng emergency plan đã được
   * PlanReceiver validate và preprocess.
   */
  currentPlan_ = emergencyPlan;
  hasActivePlan_ = true;

  if (phaseId == PhaseId::NS_GREEN) {
    if (emergencyPlan.isEmergencyNS) {
      /*
       * Emergency cùng hướng:
       * cập nhật thời gian GREEN NS.
       */
      for (std::uint8_t index = 0U; index < currentPlan_.phaseCount; ++index) {
        if (currentPlan_.phases[index].phaseId == PhaseId::NS_GREEN) {
          currentPhaseIndex_ = index;
          remainingTimeMs_ = currentPlan_.phases[index].durationMs;
          return;
        }
      }
    }

    if (emergencyPlan.isEmergencyEW) {
      /*
       * Emergency hướng đối diện:
       * kết thúc GREEN hiện tại.
       *
       * processTick() sẽ gọi advancePhase() và chuyển
       * sang YELLOW.
       */
      remainingTimeMs_ = 0U;
      return;
    }
  }

  if (phaseId == PhaseId::EW_GREEN) {
    if (emergencyPlan.isEmergencyEW) {
      /*
       * Emergency cùng hướng:
       * cập nhật thời gian GREEN EW.
       */
      for (std::uint8_t index = 0U; index < currentPlan_.phaseCount; ++index) {
        if (currentPlan_.phases[index].phaseId == PhaseId::EW_GREEN) {
          currentPhaseIndex_ = index;
          remainingTimeMs_ = currentPlan_.phases[index].durationMs;
          return;
        }
      }
    }

    if (emergencyPlan.isEmergencyNS) {
      remainingTimeMs_ = 0U;
    }
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