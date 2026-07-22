#ifndef DECISION_ENGINE_H
#define DECISION_ENGINE_H

#include <cstdint>

#include "decision_constants.h"
#include "decision_types.h"

#define DECISION_STEPS

//
// Internal helper
//
class ConstraintManager {
 public:
  ConstraintManager();
  ~ConstraintManager() = default;
  TimingPlan applyTimingConstraints(const TimingPlan& draftPlan);

 private:
  bool validateCycleLength(const TimingPlan& plan) const;
  TimingPlan scaleGreenTime(const TimingPlan& plan);
  std::uint32_t calculateCycleLength(const TimingPlan& plan) const;
  std::uint32_t minimumGreenMs{traffic_timing_decision::toMilliseconds(
      traffic_timing_decision::TimingMilliseconds::kMinimumGreen)};
  std::uint32_t maximumGreenMs{traffic_timing_decision::toMilliseconds(
      traffic_timing_decision::TimingMilliseconds::kMaximumGreen)};
  std::uint32_t maximumCycleLengthMs{traffic_timing_decision::toMilliseconds(
      traffic_timing_decision::TimingMilliseconds::kMaximumCycle)};
  std::uint32_t yellowTimeMs{traffic_timing_decision::toMilliseconds(
      traffic_timing_decision::TimingMilliseconds::kYellow)};
  std::uint32_t allRedTimeMs{traffic_timing_decision::toMilliseconds(
      traffic_timing_decision::TimingMilliseconds::kAllRed)};
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
  bool detectEmergency(const TrafficSnapshot& snapshot) const;
  TimingPlan setEmergencyFlags(const TimingPlan& previousPlan,
                               const TrafficSnapshot& snapshot);
  //----------------------------------------
  // traffic estimation
  //----------------------------------------
  DemandScore calculateDemandScore(const TrafficSnapshot& snapshot) const;
  TimingPlan assignGreenTime(const DemandScore& score);
  TimingPlan generateDraftTimingPlan(const DemandScore& score);
  std::uint32_t targetGreenTimeMs(float demandScore) const;
  std::uint32_t stepToward(std::uint32_t current, std::uint32_t target) const;
  std::uint64_t nextPlanId_{traffic_timing_decision::toIdentifier(
      traffic_timing_decision::PlanSequence::kFirstIdentifier)};
  TimingPlan previousPlan;
  DemandScore currentScore;
  bool emergencyActive{false};
  ConstraintManager constraintManager;
};

#endif  // !DECISION_ENGINE_H
