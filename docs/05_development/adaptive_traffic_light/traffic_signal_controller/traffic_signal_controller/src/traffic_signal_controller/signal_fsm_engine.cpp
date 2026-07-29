#include "traffic_signal_controller/signal_fsm_engine.h"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <ctime>

#include "common/logging_contexts.h"
#include "score/mw/log/logger.h"

namespace {

constexpr std::uint64_t kNanosecondsPerMillisecond{1'000'000ULL};

constexpr std::uint64_t kNanosecondsPerSecond{1'000'000'000ULL};

std::uint64_t timespecToNanoseconds(const timespec& timestamp) noexcept {
  return static_cast<std::uint64_t>(timestamp.tv_sec) * kNanosecondsPerSecond +
         static_cast<std::uint64_t>(timestamp.tv_nsec);
}

score::mw::log::Logger& Logger() {
  static auto& logger =
      score::mw::log::CreateLogger(ctrl::logging::kCtxFsm, "FSM");
  return logger;
}

void logFsmExecutionTime(
    const std::chrono::steady_clock::time_point executionStart) {
  const auto executionEnd = std::chrono::steady_clock::now();
  const auto measuredDuration =
      std::chrono::duration_cast<std::chrono::nanoseconds>(executionEnd -
                                                           executionStart);
  const std::uint64_t executionTimeNs =
      measuredDuration.count() > 0
          ? static_cast<std::uint64_t>(measuredDuration.count())
          : 0ULL;

  Logger().LogInfo() << "event=FSM_EXECUTION"
                     << ", execution_time_ns=" << executionTimeNs;
}

const char* phaseToString(const PhaseId phaseId) noexcept {
  switch (phaseId) {
    case PhaseId::NS_GREEN:
      return "NS_GREEN";

    case PhaseId::EW_GREEN:
      return "EW_GREEN";

    case PhaseId::YELLOW:
      return "YELLOW";

    case PhaseId::ALL_RED:
      return "ALL_RED";
  }

  return "UNKNOWN";
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
  }

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

  return SignalDisplay{currentPhaseId(), remainingTimeMs_};
}

void SignalFSMEngine::reset() noexcept {
  currentPlan_ = MakeDefaultPlan();

  currentPhaseIndex_ = 0U;

  remainingTimeMs_ = currentPlan_.phases[currentPhaseIndex_].durationMs;

  hasActivePlan_ = true;

  nextDeadline_ = timespec{};
  deadlineInitialized_ = false;

  wakeupSampleCount_ = 0U;
  droppedWakeupSampleCount_ = 0U;

  syncChannel_.SetCurrentPhase(currentPhaseId());
}

void SignalFSMEngine::dumpWakeupSamplesToLog() {
  for (std::size_t index = 0U; index < wakeupSampleCount_; ++index) {
    const WakeupSample& sample = wakeupSamples_[index];

    Logger().LogInfo() << "event=FSM_WAKEUP"
                       << ", deadline_ns=" << sample.deadlineNs
                       << ", actual_wakeup_ns=" << sample.actualWakeupNs
                       << ", latency_ns=" << sample.latencyNs;
  }

  if (droppedWakeupSampleCount_ > 0U) {
    Logger().LogWarn() << "event=FSM_WAKEUP_SAMPLES_DROPPED"
                       << ", count=" << droppedWakeupSampleCount_;
  }

  wakeupSampleCount_ = 0U;
  droppedWakeupSampleCount_ = 0U;
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
  Logger().LogInfo() << "event=PLAN_APPLIED"
                     << ", plan_id=" << pendingPlan.sourcePlanId;

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
  Logger().LogInfo() << "event=PHASE_EXIT"
                     << ", phase=" << phaseToString(currentPhaseId())
                     << ", plan_id=" << currentPlan_.sourcePlanId;
  ++currentPhaseIndex_;

  if (currentPhaseIndex_ >= currentPlan_.phaseCount) {
    currentPhaseIndex_ = 0U;
  }

  remainingTimeMs_ = currentPlan_.phases[currentPhaseIndex_].durationMs;
  Logger().LogInfo() << "event=PHASE_ENTER"
                     << ", phase=" << phaseToString(currentPhaseId())
                     << ", duration_ms=" << remainingTimeMs_
                     << ", plan_id=" << currentPlan_.sourcePlanId;

  syncChannel_.SetCurrentPhase(currentPhaseId());
}

void SignalFSMEngine::processGreenPhase(const timespec& absoluteDeadline) {
  PlanData emergencyPlan{};

  const PlanSyncChannel::WaitResult result =
      syncChannel_.WaitForEmergencyUntil(absoluteDeadline, emergencyPlan);

  // Bắt đầu đo sau khi hàm chờ trả về để không tính thời gian blocking.
  const auto executionStart = std::chrono::steady_clock::now();

  switch (result) {
    case PlanSyncChannel::WaitResult::EMERGENCY_AVAILABLE: {
      const EmergencyEvaluationResult evaluationResult =
          evaluateEmergencyPlan(emergencyPlan);

      if (evaluationResult == EmergencyEvaluationResult::ACCEPTED) {
        Logger().LogInfo() << "event=EMERGENCY_ACCEPTED"
                           << ", plan_id=" << emergencyPlan.sourcePlanId
                           << ", phase=" << phaseToString(currentPhaseId())
                           << ", remaining_ms=" << remainingTimeMs_;

        applyEmergencyPlan(emergencyPlan);
      } else {
        Logger().LogWarn() << "event=EMERGENCY_REJECTED"
                           << ", plan_id=" << emergencyPlan.sourcePlanId
                           << ", phase=" << phaseToString(currentPhaseId())
                           << ", remaining_ms=" << remainingTimeMs_
                           << ", reason="
                           << emergencyEvaluationResultToString(
                                  evaluationResult);
      }

      // Chốt sample trước khi quay lại WaitForEmergencyUntil(), tránh cộng
      // thời gian chờ tiếp theo vào execution time hiện tại.
      logFsmExecutionTime(executionStart);

      if (remainingTimeMs_ > 0U) {
        processGreenPhase(absoluteDeadline);
      }

      break;
    }

    case PlanSyncChannel::WaitResult::TIMEOUT:
      recordWakeupSample(absoluteDeadline);
      decrementRemainingTime();
      logFsmExecutionTime(executionStart);
      break;

    case PlanSyncChannel::WaitResult::ERROR:
      Logger().LogWarn() << "event=EMERGENCY_WAIT_ERROR"
                         << ", phase=" << phaseToString(currentPhaseId())
                         << ", remaining_ms=" << remainingTimeMs_;
      logFsmExecutionTime(executionStart);
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

  // Bắt đầu đo sau khi clock_nanosleep() trả về.
  const auto executionStart = std::chrono::steady_clock::now();

  if (result == 0) {
    recordWakeupSample(absoluteDeadline);
    decrementRemainingTime();
  } else {
    Logger().LogWarn() << "event=FSM_SLEEP_ERROR"
                       << ", error_code=" << result;
  }

  logFsmExecutionTime(executionStart);
}

const char* SignalFSMEngine::emergencyEvaluationResultToString(
    const EmergencyEvaluationResult result) noexcept {
  switch (result) {
    case EmergencyEvaluationResult::ACCEPTED:
      return "ACCEPTED";

    case EmergencyEvaluationResult::NO_ACTIVE_PLAN:
      return "NO_ACTIVE_PLAN";

    case EmergencyEvaluationResult::INVALID_DIRECTION_FLAGS:
      return "INVALID_DIRECTION_FLAGS";

    case EmergencyEvaluationResult::TOO_EARLY:
      return "TOO_EARLY";

    case EmergencyEvaluationResult::TOO_LATE:
      return "TOO_LATE";

    case EmergencyEvaluationResult::WRONG_DIRECTION:
      return "WRONG_DIRECTION";

    case EmergencyEvaluationResult::NOT_GREEN_PHASE:
      return "NOT_GREEN_PHASE";
  }

  return "UNKNOWN";
}

SignalFSMEngine::EmergencyEvaluationResult
SignalFSMEngine::evaluateEmergencyPlan(const PlanData& emergencyPlan) const {
  if (!hasActivePlan_) {
    return EmergencyEvaluationResult::NO_ACTIVE_PLAN;
  }

  /*
   * Emergency phải có đúng một hướng:
   * NS=true, EW=false hoặc NS=false, EW=true.
   */
  if (emergencyPlan.isEmergencyNS == emergencyPlan.isEmergencyEW) {
    return EmergencyEvaluationResult::INVALID_DIRECTION_FLAGS;
  }

  constexpr std::uint32_t kLowerThresholdMs{5'000U};
  constexpr std::uint32_t kUpperThresholdMs{10'000U};

  /*
   * Chỉ chấp nhận khi:
   *
   * 5 giây < remainingTimeMs_ < 10 giây.
   */
  if (remainingTimeMs_ >= kUpperThresholdMs) {
    return EmergencyEvaluationResult::TOO_EARLY;
  }

  if (remainingTimeMs_ <= kLowerThresholdMs) {
    return EmergencyEvaluationResult::TOO_LATE;
  }

  const PhaseId phaseId = currentPhaseId();

  if (phaseId == PhaseId::NS_GREEN) {
    if (!emergencyPlan.isEmergencyNS) {
      return EmergencyEvaluationResult::WRONG_DIRECTION;
    }

    return EmergencyEvaluationResult::ACCEPTED;
  }

  if (phaseId == PhaseId::EW_GREEN) {
    if (!emergencyPlan.isEmergencyEW) {
      return EmergencyEvaluationResult::WRONG_DIRECTION;
    }

    return EmergencyEvaluationResult::ACCEPTED;
  }

  return EmergencyEvaluationResult::NOT_GREEN_PHASE;
}

void SignalFSMEngine::applyEmergencyPlan(const PlanData& emergencyPlan) {
  constexpr std::uint32_t kEmergencyGreenDurationMs{20'000U};

  const PhaseId phaseId = currentPhaseId();
  const std::uint32_t oldRemainingTimeMs = remainingTimeMs_;

  if (phaseId == PhaseId::NS_GREEN && emergencyPlan.isEmergencyNS) {
    remainingTimeMs_ = kEmergencyGreenDurationMs;
    Logger().LogInfo() << "event=EMERGENCY_APPLIED"
                       << ", plan_id=" << emergencyPlan.sourcePlanId
                       << ", phase=" << phaseToString(phaseId)
                       << ", old_remaining_ms=" << oldRemainingTimeMs
                       << ", new_remaining_ms=" << remainingTimeMs_;
    return;
  }

  if (phaseId == PhaseId::EW_GREEN && emergencyPlan.isEmergencyEW) {
    remainingTimeMs_ = kEmergencyGreenDurationMs;
    Logger().LogInfo() << "event=EMERGENCY_APPLIED"
                       << ", plan_id=" << emergencyPlan.sourcePlanId
                       << ", phase=" << phaseToString(phaseId)
                       << ", old_remaining_ms=" << oldRemainingTimeMs
                       << ", new_remaining_ms=" << remainingTimeMs_;
  }
}

void SignalFSMEngine::decrementRemainingTime() noexcept {
  if (remainingTimeMs_ <= TIMER_INTERVAL_MS) {
    remainingTimeMs_ = 0U;
    return;
  }

  remainingTimeMs_ -= TIMER_INTERVAL_MS;
}

void SignalFSMEngine::recordWakeupSample(
    const timespec& absoluteDeadline) noexcept {
  timespec actualWakeupTime{};

  if (clock_gettime(CLOCK_MONOTONIC, &actualWakeupTime) != 0) {
    ++droppedWakeupSampleCount_;
    return;
  }

  const std::uint64_t deadlineNs = timespecToNanoseconds(absoluteDeadline);
  const std::uint64_t actualWakeupNs = timespecToNanoseconds(actualWakeupTime);

  if (actualWakeupNs < deadlineNs) {
    return;
  }

  if (wakeupSampleCount_ >= wakeupSamples_.size()) {
    ++droppedWakeupSampleCount_;
    return;
  }

  wakeupSamples_[wakeupSampleCount_] =
      WakeupSample{deadlineNs, actualWakeupNs, actualWakeupNs - deadlineNs};
  ++wakeupSampleCount_;
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