#include "decision_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#ifdef DECISION_STEPS
#include "application_logger.h"
#endif
#include "common.h"

namespace {

constexpr float kMetricPercentageMultiplier = 100.0F;
constexpr float kVehicleScorePerAverageVehicle = 6.0F;
constexpr float kMaximumComponentScore = 100.0F;
constexpr float kQueueLengthWeight = 0.2F;
constexpr float kVehicleCountWeight = 0.6F;
constexpr float kOccupancyWeight = 0.2F;

float mean(const float first, const float second) noexcept {
  return (first + second) * traffic_timing_decision::kPairMeanMultiplier;
}
float mean(const std::uint32_t first, const std::uint32_t second) noexcept {
  return static_cast<float>(first + second) *
         traffic_timing_decision::kPairMeanMultiplier;
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
    const TimingPlan unconstrained = constrained;
    constrained = scaleGreenTime(constrained);
    const std::uint32_t intergreenTime =
        traffic_timing_decision::toCount(
            traffic_timing_decision::IntersectionLayout::
                kDirectionalPhaseCount) *
        (yellowTimeMs + allRedTimeMs);
    const std::uint32_t availableGreen =
        maximumCycleLengthMs > intergreenTime
            ? maximumCycleLengthMs - intergreenTime
            : std::uint32_t{};
#ifdef DECISION_STEPS
    traffic_timing_decision::decisionLogger().LogWarn()
        << "[DECISION][CONSTRAINT][PROPORTIONAL_REDUCTION]"
        << " draft_ns_green_ms=" << unconstrained.greenNorthSouthMs
        << "; draft_ew_green_ms=" << unconstrained.greenEastWestMs
        << "; draft_cycle_ms=" << unconstrained.cycleLengthMs
        << "; maximum_cycle_ms=" << maximumCycleLengthMs
        << "; available_green_ms=" << availableGreen
        << "; scaled_ns_green_ms=" << constrained.greenNorthSouthMs
        << "; scaled_ew_green_ms=" << constrained.greenEastWestMs
        << "; scaled_cycle_ms=" << calculateCycleLength(constrained);
#endif
  }
  constrained.cycleLengthMs = calculateCycleLength(constrained);
  return constrained;
}

bool ConstraintManager::validateCycleLength(const TimingPlan& plan) const {
  return calculateCycleLength(plan) <= maximumCycleLengthMs;
}

TimingPlan ConstraintManager::scaleGreenTime(const TimingPlan& plan) {
  TimingPlan scaled = plan;
  const std::uint32_t intergreenTime =
      traffic_timing_decision::toCount(
          traffic_timing_decision::IntersectionLayout::kDirectionalPhaseCount) *
      (yellowTimeMs + allRedTimeMs);
  if (maximumCycleLengthMs <= intergreenTime) {
    scaled.greenNorthSouthMs = minimumGreenMs;
    scaled.greenEastWestMs = minimumGreenMs;
    return scaled;
  }

  const std::uint32_t availableGreen = maximumCycleLengthMs - intergreenTime;
  const std::uint64_t totalGreen =
      static_cast<std::uint64_t>(plan.greenNorthSouthMs) + plan.greenEastWestMs;
  if (totalGreen <= availableGreen || totalGreen == std::uint64_t{}) {
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
      static_cast<std::uint64_t>(traffic_timing_decision::toCount(
          traffic_timing_decision::IntersectionLayout::
              kDirectionalPhaseCount)) *
          (static_cast<std::uint64_t>(plan.yellowMs) + plan.allRedMs);
  return static_cast<std::uint32_t>(std::min<std::uint64_t>(
      value, std::numeric_limits<std::uint32_t>::max()));
}

//
// DecisionEngine
//

DecisionEngine::DecisionEngine() {
  previousPlan.greenNorthSouthMs = traffic_timing_decision::toMilliseconds(
      traffic_timing_decision::TimingMilliseconds::kInitialGreen);
  previousPlan.greenEastWestMs = traffic_timing_decision::toMilliseconds(
      traffic_timing_decision::TimingMilliseconds::kInitialGreen);
  previousPlan.yellowMs = traffic_timing_decision::toMilliseconds(
      traffic_timing_decision::TimingMilliseconds::kYellow);
  previousPlan.allRedMs = traffic_timing_decision::toMilliseconds(
      traffic_timing_decision::TimingMilliseconds::kAllRed);
  previousPlan.cycleLengthMs =
      previousPlan.greenNorthSouthMs + previousPlan.greenEastWestMs +
      traffic_timing_decision::toCount(
          traffic_timing_decision::IntersectionLayout::kDirectionalPhaseCount) *
          (previousPlan.yellowMs + previousPlan.allRedMs);
}

TimingPlan DecisionEngine::processTrafficMetrics(
    const TrafficSnapshot& snapshot) {
  emergencyActive = detectEmergency(snapshot);
  TimingPlan nextPlan{};
  if (emergencyActive) {
#ifdef DECISION_STEPS
    traffic_timing_decision::decisionLogger().LogWarn()
        << "[DECISION][EMERGENCY_OVERRIDE] frame_id=" << snapshot.frameId
        << "; action=keep_previous_green_and_forward_emergency_flags"
        << "; previous_ns_green_ms=" << previousPlan.greenNorthSouthMs
        << "; previous_ew_green_ms=" << previousPlan.greenEastWestMs
        << "; emergency_north=" << snapshot.emergencyNorth
        << "; emergency_south=" << snapshot.emergencySouth
        << "; emergency_east=" << snapshot.emergencyEast
        << "; emergency_west=" << snapshot.emergencyWest;
#endif
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
      mean(snapshot.queueLengthNorth, snapshot.queueLengthSouth) *
      kMetricPercentageMultiplier;
  const float eastWestQueue =
      mean(snapshot.queueLengthEast, snapshot.queueLengthWest) *
      kMetricPercentageMultiplier;
  const float northSouthVehicleAverage =
      mean(snapshot.vehicleCountNorth, snapshot.vehicleCountSouth);
  const float eastWestVehicleAverage =
      mean(snapshot.vehicleCountEast, snapshot.vehicleCountWest);

  // Vehicle count is averaged across both lanes: (N+S)/2 or (E+W)/2.
  // With low queue/occupancy: avg < 7 -> 20 s, avg 10 -> 30 s,
  // avg 17-20 -> 40 s. Strong queue/occupancy can raise the target to 50 s.
  const float northSouthVehicleScore = std::min(
      northSouthVehicleAverage * kVehicleScorePerAverageVehicle,
      kMaximumComponentScore);
  const float eastWestVehicleScore = std::min(
      eastWestVehicleAverage * kVehicleScorePerAverageVehicle,
      kMaximumComponentScore);
  const float northSouthOccupancy =
      mean(snapshot.occupancyNorth, snapshot.occupancySouth) *
      kMetricPercentageMultiplier;
  const float eastWestOccupancy =
      mean(snapshot.occupancyEast, snapshot.occupancyWest) *
      kMetricPercentageMultiplier;

  score.northSouthScore =
      kQueueLengthWeight * northSouthQueue +
      kVehicleCountWeight * northSouthVehicleScore +
      kOccupancyWeight * northSouthOccupancy;
  score.eastWestScore =
      kQueueLengthWeight * eastWestQueue +
      kVehicleCountWeight * eastWestVehicleScore +
      kOccupancyWeight * eastWestOccupancy;
  score.overallScore = (score.northSouthScore + score.eastWestScore) *
                       traffic_timing_decision::kPairMeanMultiplier;

  const std::uint32_t northSouthTargetMs =
      targetGreenTimeMs(score.northSouthScore);
  const std::uint32_t eastWestTargetMs = targetGreenTimeMs(score.eastWestScore);
#ifdef DECISION_STEPS
  traffic_timing_decision::decisionLogger().LogInfo()
      << "[DECISION][DEMAND_SCORE] frame_id=" << snapshot.frameId
      << "; ns_score=" << score.northSouthScore
      << "; ns_target_green_ms=" << northSouthTargetMs
      << "; ew_score=" << score.eastWestScore
      << "; ew_target_green_ms=" << eastWestTargetMs
      << "; overall_score=" << score.overallScore
      << "; weights=queue:0.2,vehicles:0.6,occupancy_pct:0.2";
#endif

  return score;
}

TimingPlan DecisionEngine::assignGreenTime(const DemandScore& score) {
  TimingPlan plan = previousPlan;
  const std::uint32_t northSouthTargetMs =
      targetGreenTimeMs(score.northSouthScore);
  const std::uint32_t eastWestTargetMs = targetGreenTimeMs(score.eastWestScore);
  plan.greenNorthSouthMs =
      stepToward(previousPlan.greenNorthSouthMs, northSouthTargetMs);
  plan.greenEastWestMs =
      stepToward(previousPlan.greenEastWestMs, eastWestTargetMs);
#ifdef DECISION_STEPS
  traffic_timing_decision::decisionLogger().LogInfo()
      << "[DECISION][GREEN_ADJUSTMENT]"
      << " ns_current_ms=" << previousPlan.greenNorthSouthMs
      << "; ns_target_ms=" << northSouthTargetMs
      << "; ns_draft_ms=" << plan.greenNorthSouthMs
      << "; ew_current_ms=" << previousPlan.greenEastWestMs
      << "; ew_target_ms=" << eastWestTargetMs
      << "; ew_draft_ms=" << plan.greenEastWestMs << "; maximum_step_ms="
      << traffic_timing_decision::toMilliseconds(
             traffic_timing_decision::TimingMilliseconds::kGreenAdjustmentStep);
#endif
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
  if (demandScore < traffic_timing_decision::kLowDemandScoreUpperBound) {
    return traffic_timing_decision::toMilliseconds(
        traffic_timing_decision::TimingMilliseconds::kLowDemandGreen);
  }
  if (demandScore < traffic_timing_decision::kModerateDemandScoreUpperBound) {
    return traffic_timing_decision::toMilliseconds(
        traffic_timing_decision::TimingMilliseconds::kModerateDemandGreen);
  }
  if (demandScore < traffic_timing_decision::kHighDemandScoreUpperBound) {
    return traffic_timing_decision::toMilliseconds(
        traffic_timing_decision::TimingMilliseconds::kHighDemandGreen);
  }
  return traffic_timing_decision::toMilliseconds(
      traffic_timing_decision::TimingMilliseconds::kVeryHighDemandGreen);
}

std::uint32_t DecisionEngine::stepToward(const std::uint32_t current,
                                         const std::uint32_t target) const {
  if (current < target) {
    return std::min(target,
                    current + traffic_timing_decision::toMilliseconds(
                                  traffic_timing_decision::TimingMilliseconds::
                                      kGreenAdjustmentStep));
  }
  if (current > target) {
    return current - std::min(traffic_timing_decision::toMilliseconds(
                                  traffic_timing_decision::TimingMilliseconds::
                                      kGreenAdjustmentStep),
                              current - target);
  }
  return current;
}
