#ifndef TRAFFIC_SIGNAL_CONTROLLER_SIGNAL_FSM_ENGINE_H_
#define TRAFFIC_SIGNAL_CONTROLLER_SIGNAL_FSM_ENGINE_H_

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ctime>

#include "common/config.h"
#include "common/plan_sync_channel.h"

class SignalFSMEngine final {
 public:
  explicit SignalFSMEngine(PlanSyncChannel& syncChannel);

  SignalDisplay processTick();
  void reset() noexcept;

  // Drain buffered wakeup samples from a non-real-time thread.
  // Safe to call while the FSM producer thread is running.
  void flushWakeupSamplesToLog();

  // Final drain after the FSM real-time thread has stopped.
  void dumpWakeupSamplesToLog();

 private:
  struct WakeupSample {
    std::uint64_t deadlineNs{0U};
    std::uint64_t actualWakeupNs{0U};
    std::uint64_t latencyNs{0U};
  };

  static constexpr std::size_t kWakeupSampleCapacity{4096U};

  enum class EmergencyEvaluationResult {
    ACCEPTED,
    NO_ACTIVE_PLAN,
    INVALID_DIRECTION_FLAGS,
    TOO_EARLY,
    TOO_LATE,
    WRONG_DIRECTION,
    NOT_GREEN_PHASE
  };
  bool loadPendingPlan();
  void advancePhase();

  void processGreenPhase(const timespec& absoluteDeadline);

  void processNonInterruptiblePhase(const timespec& absoluteDeadline);

  EmergencyEvaluationResult evaluateEmergencyPlan(
      const PlanData& emergencyPlan) const;

  static const char* emergencyEvaluationResultToString(
    EmergencyEvaluationResult result) noexcept;
    
  void applyEmergencyPlan(const PlanData& emergencyPlan);

  void decrementRemainingTimeBy(std::uint32_t elapsedMs) noexcept;

  std::uint64_t recordWakeupSample(const timespec& absoluteDeadline) noexcept;

  std::uint32_t elapsedMillisecondsFromLatency(
      std::uint64_t latencyNs) noexcept;

  void initializeDeadline();

  static void addMilliseconds(timespec& timestamp,
                              std::uint32_t milliseconds) noexcept;

  PhaseId currentPhaseId() const noexcept;

  static PlanData createDefaultPlan();

  PlanSyncChannel& syncChannel_;

  PlanData currentPlan_{};
  std::uint32_t remainingTimeMs_{0U};
  std::uint8_t currentPhaseIndex_{0U};
  bool hasActivePlan_{false};

  timespec nextDeadline_{};
  bool deadlineInitialized_{false};
  bool initialPhaseLogged_{false};

  // Single-producer/single-consumer ring buffer:
  // producer = FSM real-time thread, consumer = lifecycle thread.
  std::array<WakeupSample, kWakeupSampleCapacity> wakeupSamples_{};
  std::atomic<std::uint64_t> wakeupWriteSequence_{0U};
  std::atomic<std::uint64_t> wakeupReadSequence_{0U};
  std::atomic<std::uint64_t> droppedWakeupSampleCount_{0U};
};

#endif  // TRAFFIC_SIGNAL_CONTROLLER_SIGNAL_FSM_ENGINE_H_
