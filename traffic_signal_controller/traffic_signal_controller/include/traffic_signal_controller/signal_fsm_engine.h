#ifndef SIGNAL_FSM_ENGINE_H
#define SIGNAL_FSM_ENGINE_H

#include <common/config.h>

struct FSMResult {
  SignalOutput output{};
  SignalDisplay display{};
};

class SignalFSMEngine {
 public:
  FSMResult processTick(const TimerTick& tick);

  HealthStatus sendHeartbeat() const;

 private:
  void fsm();

  uint32_t remainingTimeMs;
  uint8_t currentPhaseIndex;

  PlanData currentPlan;

  bool isEmergencyNS;
  bool isEmergencyEW;

  bool hasNewPlan;
};

#endif