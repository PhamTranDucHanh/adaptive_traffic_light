#ifndef SIGNAL_FSM_ENGINE_H
#define SIGNAL_FSM_ENGINE_H

#include <common/config.h>

class SignalFSMEngine {
 public:
  SignalDisplay processTick();

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