#ifndef SIGNAL_FSM_ENGINE_H
#define SIGNAL_FSM_ENGINE_H

#include <common/config.h>
#include <common/plan_sync_channel.h>
#include <time.h>

class SignalFSMEngine {
 public:
  explicit SignalFSMEngine(PlanSyncChannel& syncChannel);
  SignalDisplay processTick();

 private:
  void fsm();

  PlanSyncChannel& syncChannel_;

  uint32_t remainingTimeMs{};
  uint8_t currentPhaseIndex{};
  PlanData currentPlan{};
  bool isEmergencyNS{};
  bool isEmergencyEW{};
  bool hasNewPlan{};
  struct timespec deadline_;
};

#endif