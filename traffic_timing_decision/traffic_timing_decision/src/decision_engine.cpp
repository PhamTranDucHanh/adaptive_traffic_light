#include "decision_engine.h"

//
// ConstraintManager
//

ConstraintManager::ConstraintManager() = default;

TimingPlan ConstraintManager::applyTimingConstraints(
    const TimingPlan& draftPlan) {
  return draftPlan;
}

bool ConstraintManager::validateCycleLength(const TimingPlan& plan) {
  (void)plan;
  return true;
}

TimingPlan ConstraintManager::scaleGreenTime(const TimingPlan& plan) {
  return plan;
}

//
// DecisionEngine
//

DecisionEngine::DecisionEngine() = default;

TimingPlan DecisionEngine::processTrafficMetrics(
    const TrafficSnapshot& snapshot) {
  (void)snapshot;
  return TimingPlan{};
}

bool DecisionEngine::detectEmergency(const TrafficSnapshot& snapshot) {
  (void)snapshot;
  return false;
}

TimingPlan DecisionEngine::setEmergencyFlags(const TimingPlan& previousPlan) {
  return previousPlan;
}

DemandScore DecisionEngine::calculateDemandScore(
    const TrafficSnapshot& snapshot) {
  (void)snapshot;
  return DemandScore{};
}

TimingPlan DecisionEngine::assignGreenTime(const DemandScore& score) {
  (void)score;
  return TimingPlan{};
}

TimingPlan DecisionEngine::generateDraftTimingPlan(const DemandScore& score) {
  (void)score;
  return TimingPlan{};
}