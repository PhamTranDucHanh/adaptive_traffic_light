#ifndef TRAFFIC_SIGNAL_CONTROLLER_SIGNAL_FSM_ENGINE_H_
#define TRAFFIC_SIGNAL_CONTROLLER_SIGNAL_FSM_ENGINE_H_

#include <cstdint>
#include <ctime>

#include "common/config.h"
#include "common/plan_sync_channel.h"

class SignalFSMEngine final {
 public:
  explicit SignalFSMEngine(PlanSyncChannel& syncChannel);

  SignalDisplay processTick();
  void reset() noexcept;

 private:
  bool loadPendingPlan();
  void advancePhase();

  void processGreenPhase(const timespec& absoluteDeadline);

  void processNonInterruptiblePhase(const timespec& absoluteDeadline);

  bool evaluateEmergencyPlan(const PlanData& emergencyPlan) const;

  void applyEmergencyPlan(const PlanData& emergencyPlan);

  void decrementRemainingTime() noexcept;

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
};

#endif  // TRAFFIC_SIGNAL_CONTROLLER_SIGNAL_FSM_ENGINE_H_