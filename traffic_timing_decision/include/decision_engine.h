#ifndef DECISION_ENGINE_H
#define DECISION_ENGINE_H

#include <cstdint>

#include "decision_types.h"

//
// Internal helper
//
class ConstraintManager {
 public:
  ConstraintManager();
  ~ConstraintManager() = default;
  TimingPlan applyTimingConstraints(const TimingPlan& draftPlan);

 private:
  bool validateCycleLength(const TimingPlan& plan);
  TimingPlan scaleGreenTime(const TimingPlan& plan);
  uint32_t minimumGreenMs{10000};
  uint32_t maximumGreenMs{60000};
  uint32_t maximumCycleLengthMs{120000};
  uint32_t yellowTimeMs{3000};
  uint32_t allRedTimeMs{1000};
};

//
// Decision Engine
//
class DecisionEngine {
 public:
  DecisionEngine();
  ~DecisionEngine() = default;
  //----------------------------------------
  // main entry
  //----------------------------------------
  TimingPlan processTrafficMetrics(const TrafficSnapshot& snapshot);

 private:
  //----------------------------------------
  // emergency
  //----------------------------------------
  bool detectEmergency(const TrafficSnapshot& snapshot);
  TimingPlan setEmergencyFlags(const TimingPlan& previousPlan);
  //----------------------------------------
  // traffic estimation
  //----------------------------------------
  DemandScore calculateDemandScore(const TrafficSnapshot& snapshot);
  TimingPlan assignGreenTime(const DemandScore& score);
  TimingPlan generateDraftTimingPlan(const DemandScore& score);
  TimingPlan previousPlan;
  DemandScore currentScore;
  bool emergencyActive{false};
  ConstraintManager constraintManager;
};

#endif  // !DECISION_ENGINE_H