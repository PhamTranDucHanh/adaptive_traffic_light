#include "traffic_signal_controller/signal_fsm_engine.h"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <limits>

#include "common/logging_contexts.h"
#include "score/mw/log/logger.h"

namespace {

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

enum class DisplayLamp : std::uint8_t { RED, YELLOW, GREEN };

DisplayLamp lampForPhase(const PhaseId phaseId,
                         const SignalGroup phaseGroup,
                         const SignalGroup requestedGroup) noexcept {
  if (phaseId == PhaseId::ALL_RED) {
    return DisplayLamp::RED;
  }
  if (phaseId == PhaseId::YELLOW) {
    return phaseGroup == requestedGroup ? DisplayLamp::YELLOW
                                        : DisplayLamp::RED;
  }
  if (phaseId == PhaseId::NS_GREEN) {
    return requestedGroup == SignalGroup::NORTH_SOUTH ? DisplayLamp::GREEN
                                                       : DisplayLamp::RED;
  }
  return requestedGroup == SignalGroup::EAST_WEST ? DisplayLamp::GREEN
                                                   : DisplayLamp::RED;
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

  if (!initialPhaseLogged_) {
    Logger().LogInfo() << "event=PHASE_ENTER"
                       << ", phase=" << phaseToString(currentPhaseId())
                       << ", duration_ms=" << remainingTimeMs_
                       << ", plan_id=" << currentPlan_.sourcePlanId;
    initialPhaseLogged_ = true;
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

  return SignalDisplay{currentPhaseId(), remainingTimeMs_, activeGroup_,
                       remainingForSignalGroup(SignalGroup::NORTH_SOUTH),
                       remainingForSignalGroup(SignalGroup::EAST_WEST)};
}

void SignalFSMEngine::reset() noexcept {
  currentPlan_ = MakeDefaultPlan();

  currentPhaseIndex_ = 0U;
  activeGroup_ = SignalGroup::NORTH_SOUTH;

  remainingTimeMs_ = currentPlan_.phases[currentPhaseIndex_].durationMs;

  hasActivePlan_ = true;

  nextDeadline_ = timespec{};
  deadlineInitialized_ = false;
  initialPhaseLogged_ = false;

  wakeupWriteSequence_.store(0U, std::memory_order_relaxed);
  wakeupReadSequence_.store(0U, std::memory_order_relaxed);
  droppedWakeupSampleCount_.store(0U, std::memory_order_relaxed);

  syncChannel_.SetCurrentPhase(currentPhaseId());
}

void SignalFSMEngine::flushWakeupSamplesToLog() {
  std::uint64_t readSequence =
      wakeupReadSequence_.load(std::memory_order_relaxed);
  const std::uint64_t writeSequence =
      wakeupWriteSequence_.load(std::memory_order_acquire);

  while (readSequence < writeSequence) {
    const std::size_t index = static_cast<std::size_t>(
        readSequence % static_cast<std::uint64_t>(kWakeupSampleCapacity));
    const WakeupSample& sample = wakeupSamples_[index];

    Logger().LogInfo() << "event=FSM_WAKEUP"
                       << ", deadline_ns=" << sample.deadlineNs
                       << ", actual_wakeup_ns=" << sample.actualWakeupNs
                       << ", latency_ns=" << sample.latencyNs;
    ++readSequence;
  }

  wakeupReadSequence_.store(readSequence, std::memory_order_release);

  const std::uint64_t droppedSamples =
      droppedWakeupSampleCount_.exchange(0U, std::memory_order_acq_rel);
  if (droppedSamples > 0U) {
    Logger().LogWarn() << "event=FSM_WAKEUP_SAMPLES_DROPPED"
                       << ", count=" << droppedSamples;
  }
}

void SignalFSMEngine::dumpWakeupSamplesToLog() {
  flushWakeupSamplesToLog();
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
    if (currentPhaseId() == PhaseId::EW_GREEN) {
      activeGroup_ = SignalGroup::EAST_WEST;
    } else {
      activeGroup_ = SignalGroup::NORTH_SOUTH;
    }

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
  if (currentPhaseId() == PhaseId::NS_GREEN) {
    activeGroup_ = SignalGroup::NORTH_SOUTH;
  } else if (currentPhaseId() == PhaseId::EW_GREEN) {
    activeGroup_ = SignalGroup::EAST_WEST;
  }
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
      decrementRemainingTimeBy(elapsedMillisecondsFromLatency(
          recordWakeupSample(absoluteDeadline)));
      logFsmExecutionTime(executionStart);
      break;

    case PlanSyncChannel::WaitResult::ERROR:
      Logger().LogWarn() << "event=EMERGENCY_WAIT_ERROR"
                         << ", phase=" << phaseToString(currentPhaseId())
                         << ", remaining_ms=" << remainingTimeMs_;
      // Shutdown/error wakeups are not completed FSM cycles.
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
    decrementRemainingTimeBy(elapsedMillisecondsFromLatency(
        recordWakeupSample(absoluteDeadline)));
    logFsmExecutionTime(executionStart);
  } else {
    Logger().LogWarn() << "event=FSM_SLEEP_ERROR"
                       << ", error_code=" << result;
    // Do not count an interrupted/failed sleep as a completed FSM cycle.
  }
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

  /*
   * Chỉ chấp nhận khi:
   *
   * 5 giây < remainingTimeMs_ < 10 giây.
   */
  if (remainingTimeMs_ >= kEmergencyUpperThresholdMs) {
    return EmergencyEvaluationResult::TOO_EARLY;
  }

  if (remainingTimeMs_ <= kEmergencyLowerThresholdMs) {
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
  const PhaseId phaseId = currentPhaseId();
  const std::uint32_t oldRemainingTimeMs = remainingTimeMs_;
  bool applied{false};

  if (phaseId == PhaseId::NS_GREEN && emergencyPlan.isEmergencyNS) {
    remainingTimeMs_ = kEmergencyGreenDurationMs;
    Logger().LogInfo() << "event=EMERGENCY_APPLIED"
                       << ", plan_id=" << emergencyPlan.sourcePlanId
                       << ", phase=" << phaseToString(phaseId)
                       << ", old_remaining_ms=" << oldRemainingTimeMs
                       << ", new_remaining_ms=" << remainingTimeMs_;
    applied = true;
  }

  if (phaseId == PhaseId::EW_GREEN && emergencyPlan.isEmergencyEW) {
    remainingTimeMs_ = kEmergencyGreenDurationMs;
    Logger().LogInfo() << "event=EMERGENCY_APPLIED"
                       << ", plan_id=" << emergencyPlan.sourcePlanId
                       << ", phase=" << phaseToString(phaseId)
                       << ", old_remaining_ms=" << oldRemainingTimeMs
                       << ", new_remaining_ms=" << remainingTimeMs_;
    applied = true;
  }

  timespec applyTime{};
  if (applied && emergencyPlan.controllerReceiveTimestampNs > 0U &&
      clock_gettime(CLOCK_MONOTONIC, &applyTime) == 0) {
    const std::uint64_t applyTimestampNs = timespecToNanoseconds(applyTime);
    if (applyTimestampNs >= emergencyPlan.controllerReceiveTimestampNs) {
      Logger().LogInfo()
          << "event=EMERGENCY_RECEIVE_TO_APPLY"
          << ", plan_id=" << emergencyPlan.sourcePlanId
          << ", receive_timestamp_ns="
          << emergencyPlan.controllerReceiveTimestampNs
          << ", apply_timestamp_ns=" << applyTimestampNs
          << ", latency_ns="
          << (applyTimestampNs - emergencyPlan.controllerReceiveTimestampNs);
    }
  }
}

void SignalFSMEngine::decrementRemainingTimeBy(
    const std::uint32_t elapsedMs) noexcept {
  if (remainingTimeMs_ <= elapsedMs) {
    remainingTimeMs_ = 0U;
    return;
  }

  remainingTimeMs_ -= elapsedMs;
}

std::uint64_t SignalFSMEngine::recordWakeupSample(
    const timespec& absoluteDeadline) noexcept {
  timespec actualWakeupTime{};

  if (clock_gettime(CLOCK_MONOTONIC, &actualWakeupTime) != 0) {
    droppedWakeupSampleCount_.fetch_add(1U, std::memory_order_relaxed);
    return 0U;
  }

  const std::uint64_t deadlineNs = timespecToNanoseconds(absoluteDeadline);
  const std::uint64_t actualWakeupNs = timespecToNanoseconds(actualWakeupTime);

  if (actualWakeupNs < deadlineNs) {
    return 0U;
  }

  const std::uint64_t latencyNs = actualWakeupNs - deadlineNs;

  const std::uint64_t writeSequence =
      wakeupWriteSequence_.load(std::memory_order_relaxed);
  const std::uint64_t readSequence =
      wakeupReadSequence_.load(std::memory_order_acquire);

  if ((writeSequence - readSequence) >=
      static_cast<std::uint64_t>(kWakeupSampleCapacity)) {
    droppedWakeupSampleCount_.fetch_add(1U, std::memory_order_relaxed);
    return latencyNs;
  }

  const std::size_t index = static_cast<std::size_t>(
      writeSequence % static_cast<std::uint64_t>(kWakeupSampleCapacity));
  wakeupSamples_[index] =
      WakeupSample{deadlineNs, actualWakeupNs, latencyNs};

  wakeupWriteSequence_.store(writeSequence + 1U, std::memory_order_release);
  return latencyNs;
}

std::uint32_t SignalFSMEngine::elapsedMillisecondsFromLatency(
    const std::uint64_t latencyNs) noexcept {
  constexpr std::uint64_t kTickPeriodNs{
      static_cast<std::uint64_t>(TIMER_INTERVAL_MS) *
      kNanosecondsPerMillisecond};
  const std::uint64_t missedIntervals = latencyNs / kTickPeriodNs;
  const std::uint64_t elapsedIntervals = missedIntervals + 1U;

  if (missedIntervals > 0U) {
    const std::uint64_t resyncMs =
        missedIntervals * static_cast<std::uint64_t>(TIMER_INTERVAL_MS);
    const std::uint32_t cappedResyncMs =
        resyncMs > std::numeric_limits<std::uint32_t>::max()
            ? std::numeric_limits<std::uint32_t>::max()
            : static_cast<std::uint32_t>(resyncMs);
    addMilliseconds(nextDeadline_, cappedResyncMs);
  }

  const std::uint64_t elapsedMs =
      elapsedIntervals * static_cast<std::uint64_t>(TIMER_INTERVAL_MS);
  return elapsedMs > std::numeric_limits<std::uint32_t>::max()
             ? std::numeric_limits<std::uint32_t>::max()
             : static_cast<std::uint32_t>(elapsedMs);
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

std::uint32_t SignalFSMEngine::remainingForSignalGroup(
    const SignalGroup group) const noexcept {
  if (!hasActivePlan_ || currentPlan_.phaseCount == 0U ||
      currentPhaseIndex_ >= currentPlan_.phaseCount) {
    return 0U;
  }

  SignalGroup phaseGroup = activeGroup_;
  const DisplayLamp currentLamp =
      lampForPhase(currentPhaseId(), phaseGroup, group);
  std::uint64_t remaining = remainingTimeMs_;

  for (std::uint8_t offset{1U}; offset < currentPlan_.phaseCount; ++offset) {
    const std::uint8_t index = static_cast<std::uint8_t>(
        (currentPhaseIndex_ + offset) % currentPlan_.phaseCount);
    const Phase& nextPhase = currentPlan_.phases[index];
    if (nextPhase.phaseId == PhaseId::NS_GREEN) {
      phaseGroup = SignalGroup::NORTH_SOUTH;
    } else if (nextPhase.phaseId == PhaseId::EW_GREEN) {
      phaseGroup = SignalGroup::EAST_WEST;
    }

    if (lampForPhase(nextPhase.phaseId, phaseGroup, group) != currentLamp) {
      break;
    }
    remaining += nextPhase.durationMs;
  }

  return remaining > std::numeric_limits<std::uint32_t>::max()
             ? std::numeric_limits<std::uint32_t>::max()
             : static_cast<std::uint32_t>(remaining);
}
