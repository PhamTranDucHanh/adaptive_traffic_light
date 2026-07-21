#include "decision_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#ifdef DECISION_ENGINE_STAT
#include "application_logger.h"
#endif
#include "common.h"

namespace {

constexpr std::uint32_t kInitialGreenMs = 30000U;
constexpr std::uint32_t kGreenAdjustmentStepMs = 5000U;

float mean(const float first, const float second) noexcept {
  return (first + second) * 0.5F;
}
float mean(const std::uint32_t first, const std::uint32_t second) noexcept {
  return static_cast<float>(first + second) * 0.5F;
}

}  // namespace

//
// ConstraintManager
//

ConstraintManager::ConstraintManager() = default;

TimingPlan ConstraintManager::applyTimingConstraints(
    const TimingPlan& draftPlan) {
  TimingPlan constrained = draftPlan;
  constrained.yellowMs = yellowTimeMs;
  constrained.allRedMs = allRedTimeMs;
  constrained.greenNorthSouthMs =
      std::clamp(constrained.greenNorthSouthMs, minimumGreenMs, maximumGreenMs);
  constrained.greenEastWestMs =
      std::clamp(constrained.greenEastWestMs, minimumGreenMs, maximumGreenMs);
  constrained.cycleLengthMs = calculateCycleLength(constrained);

  if (!validateCycleLength(constrained)) {
    constrained = scaleGreenTime(constrained);
  }
  constrained.cycleLengthMs = calculateCycleLength(constrained);
  return constrained;
}

bool ConstraintManager::validateCycleLength(const TimingPlan& plan) const {
  return calculateCycleLength(plan) <= maximumCycleLengthMs;
}

TimingPlan ConstraintManager::scaleGreenTime(const TimingPlan& plan) {
  TimingPlan scaled = plan;
  const std::uint32_t intergreenTime = 2U * (yellowTimeMs + allRedTimeMs);
  if (maximumCycleLengthMs <= intergreenTime) {
    scaled.greenNorthSouthMs = minimumGreenMs;
    scaled.greenEastWestMs = minimumGreenMs;
    return scaled;
  }

  const std::uint32_t availableGreen = maximumCycleLengthMs - intergreenTime;
  const std::uint64_t totalGreen =
      static_cast<std::uint64_t>(plan.greenNorthSouthMs) + plan.greenEastWestMs;
  if (totalGreen <= availableGreen || totalGreen == 0U) {
    return scaled;
  }

  std::uint32_t northSouth = static_cast<std::uint32_t>(
      (static_cast<std::uint64_t>(plan.greenNorthSouthMs) * availableGreen) /
      totalGreen);
  northSouth =
      std::clamp(northSouth, minimumGreenMs, availableGreen - minimumGreenMs);
  const std::uint32_t eastWest = availableGreen - northSouth;
  scaled.greenNorthSouthMs = northSouth;
  scaled.greenEastWestMs = eastWest;
  return scaled;
}

std::uint32_t ConstraintManager::calculateCycleLength(
    const TimingPlan& plan) const {
  const std::uint64_t value =
      static_cast<std::uint64_t>(plan.greenNorthSouthMs) +
      plan.greenEastWestMs +
      2ULL * (static_cast<std::uint64_t>(plan.yellowMs) + plan.allRedMs);
  return static_cast<std::uint32_t>(std::min<std::uint64_t>(value, UINT32_MAX));
}

//
// DecisionEngine
//

DecisionEngine::DecisionEngine() {
  previousPlan.greenNorthSouthMs = kInitialGreenMs;
  previousPlan.greenEastWestMs = kInitialGreenMs;
  previousPlan.yellowMs = 3000U;
  previousPlan.allRedMs = 1000U;
  previousPlan.cycleLengthMs =
      previousPlan.greenNorthSouthMs + previousPlan.greenEastWestMs +
      2U * (previousPlan.yellowMs + previousPlan.allRedMs);
}

TimingPlan DecisionEngine::processTrafficMetrics(
    const TrafficSnapshot& snapshot) {
  emergencyActive = detectEmergency(snapshot);
  TimingPlan nextPlan{};
  if (emergencyActive) {
    nextPlan = setEmergencyFlags(previousPlan, snapshot);
  } else {
    currentScore = calculateDemandScore(snapshot);
    nextPlan = generateDraftTimingPlan(currentScore);
  }

  nextPlan = constraintManager.applyTimingConstraints(nextPlan);
  previousPlan = nextPlan;
  return previousPlan;
}

bool DecisionEngine::detectEmergency(const TrafficSnapshot& snapshot) const {
  return snapshot.emergencyNorth || snapshot.emergencySouth ||
         snapshot.emergencyEast || snapshot.emergencyWest;
}

TimingPlan DecisionEngine::setEmergencyFlags(const TimingPlan& previous,
                                             const TrafficSnapshot& snapshot) {
  TimingPlan emergencyPlan = previous;
  emergencyPlan.planId = nextPlanId_++;
  emergencyPlan.generationTimestampNs = common::monotonicNanoseconds();
  emergencyPlan.emergencyNorthSouth =
      snapshot.emergencyNorth || snapshot.emergencySouth;
  emergencyPlan.emergencyEastWest =
      snapshot.emergencyEast || snapshot.emergencyWest;
  return emergencyPlan;
}

DemandScore DecisionEngine::calculateDemandScore(
    const TrafficSnapshot& snapshot) const {
  DemandScore score{};
  const float northSouthQueue =
      mean(snapshot.queueLengthNorth, snapshot.queueLengthSouth);
  const float eastWestQueue =
      mean(snapshot.queueLengthEast, snapshot.queueLengthWest);
  const float northSouthVehicles =
      mean(snapshot.vehicleCountNorth, snapshot.vehicleCountSouth);
  const float eastWestVehicles =
      mean(snapshot.vehicleCountEast, snapshot.vehicleCountWest);
  const float northSouthOccupancy =
      mean(snapshot.occupancyNorth, snapshot.occupancySouth) * 100.0F;
  const float eastWestOccupancy =
      mean(snapshot.occupancyEast, snapshot.occupancyWest) * 100.0F;

  score.northSouthScore = 0.5F * northSouthQueue + 0.3F * northSouthVehicles +
                          0.2F * northSouthOccupancy;
  score.eastWestScore =
      0.5F * eastWestQueue + 0.3F * eastWestVehicles + 0.2F * eastWestOccupancy;
  score.overallScore = (score.northSouthScore + score.eastWestScore) * 0.5F;

#ifdef DECISION_ENGINE_STAT
  traffic_timing_decision::applicationLogger().LogInfo()
      << "[DECISION][DEMAND_SCORE] frame_id=" << snapshot.frameId
      << "; ns_score=" << score.northSouthScore
      << "; ew_score=" << score.eastWestScore
      << "; overall_score=" << score.overallScore
      << "; weights=queue:0.5,vehicles:0.3,occupancy_pct:0.2";
#endif

  return score;
}

TimingPlan DecisionEngine::assignGreenTime(const DemandScore& score) {
  TimingPlan plan = previousPlan;
  plan.greenNorthSouthMs = stepToward(previousPlan.greenNorthSouthMs,
                                      targetGreenTimeMs(score.northSouthScore));
  plan.greenEastWestMs = stepToward(previousPlan.greenEastWestMs,
                                    targetGreenTimeMs(score.eastWestScore));
  return plan;
}

TimingPlan DecisionEngine::generateDraftTimingPlan(const DemandScore& score) {
  TimingPlan draft = assignGreenTime(score);
  draft.planId = nextPlanId_++;
  draft.generationTimestampNs = common::monotonicNanoseconds();
  draft.emergencyNorthSouth = false;
  draft.emergencyEastWest = false;
  return draft;
}

std::uint32_t DecisionEngine::targetGreenTimeMs(const float demandScore) const {
  if (demandScore < 30.0F) {
    return 20000U;
  }
  if (demandScore < 60.0F) {
    return 30000U;
  }
  if (demandScore < 90.0F) {
    return 40000U;
  }
  return 55000U;
}

std::uint32_t DecisionEngine::stepToward(const std::uint32_t current,
                                         const std::uint32_t target) const {
  if (current < target) {
    return std::min(target, current + kGreenAdjustmentStepMs);
  }
  if (current > target) {
    return current - std::min(kGreenAdjustmentStepMs, current - target);
  }
  return current;
}
