#include <cstdint>

#include "common.h"
#include "decision_engine.h"
#include "traffic_data_receiver.h"

namespace {

struct DirectionTraffic {
  std::uint32_t vehicles;
  float queueLength;
  float occupancy;
};

constexpr DirectionTraffic kScore20{20U, 20.0F, 0.20F};
constexpr DirectionTraffic kScore30{40U, 20.0F, 0.40F};
constexpr DirectionTraffic kScore60{60U, 60.0F, 0.60F};
constexpr DirectionTraffic kScore90{80U, 100.0F, 0.80F};

TrafficSnapshot makeSnapshot(const std::uint64_t frameId,
                             const DirectionTraffic& northSouth,
                             const DirectionTraffic& eastWest) {
  TrafficSnapshot snapshot{};
  snapshot.frameId = frameId;
  snapshot.timestampUs = frameId;
  snapshot.vehicleCountNorth = northSouth.vehicles;
  snapshot.vehicleCountSouth = northSouth.vehicles;
  snapshot.vehicleCountEast = eastWest.vehicles;
  snapshot.vehicleCountWest = eastWest.vehicles;
  snapshot.queueLengthNorth = northSouth.queueLength;
  snapshot.queueLengthSouth = northSouth.queueLength;
  snapshot.queueLengthEast = eastWest.queueLength;
  snapshot.queueLengthWest = eastWest.queueLength;
  snapshot.occupancyNorth = northSouth.occupancy;
  snapshot.occupancySouth = northSouth.occupancy;
  snapshot.occupancyEast = eastWest.occupancy;
  snapshot.occupancyWest = eastWest.occupancy;
  return snapshot;
}

TimingPlan processRepeated(DecisionEngine& engine,
                           const DirectionTraffic& northSouth,
                           const DirectionTraffic& eastWest,
                           const std::uint32_t repeatCount,
                           std::uint64_t& nextFrameId) {
  TimingPlan plan{};
  for (std::uint32_t index = 0U; index < repeatCount; ++index) {
    plan = engine.processTrafficMetrics(
        makeSnapshot(nextFrameId++, northSouth, eastWest));
  }
  return plan;
}

bool hasGreenTimes(const TimingPlan& plan, const std::uint32_t northSouthMs,
                   const std::uint32_t eastWestMs) {
  return plan.greenNorthSouthMs == northSouthMs &&
         plan.greenEastWestMs == eastWestMs;
}

}  // namespace

int main() {
  DecisionEngine engine{};
  std::uint64_t nextFrameId{1U};

  // Exercise every demand bucket and both adjustment directions. The repeat
  // counts mirror perception_demo and account for the five-second step limit.
  TimingPlan plan =
      processRepeated(engine, kScore20, kScore20, 2U, nextFrameId);
  if (!hasGreenTimes(plan, 20000U, 20000U)) {
    return 1;
  }

  plan = processRepeated(engine, kScore30, kScore30, 2U, nextFrameId);
  if (!hasGreenTimes(plan, 30000U, 30000U)) {
    return 2;
  }

  plan = processRepeated(engine, kScore60, kScore60, 2U, nextFrameId);
  if (!hasGreenTimes(plan, 40000U, 40000U)) {
    return 3;
  }

  plan = processRepeated(engine, kScore90, kScore20, 4U, nextFrameId);
  if (!hasGreenTimes(plan, 50000U, 20000U)) {
    return 4;
  }

  plan = processRepeated(engine, kScore20, kScore90, 6U, nextFrameId);
  if (!hasGreenTimes(plan, 20000U, 50000U)) {
    return 5;
  }

  plan = processRepeated(engine, kScore30, kScore30, 4U, nextFrameId);
  if (!hasGreenTimes(plan, 30000U, 30000U)) {
    return 6;
  }

  // Both targets are 50 s. Their 108 s draft cycle exceeds the 100 s cap, so
  // 92 s of available green is divided proportionally as 46 s / 46 s.
  plan = processRepeated(engine, kScore90, kScore90, 4U, nextFrameId);
  if (!hasGreenTimes(plan, 46000U, 46000U) || plan.cycleLengthMs != 100000U) {
    return 7;
  }

  plan = processRepeated(engine, kScore60, kScore60, 2U, nextFrameId);
  if (!hasGreenTimes(plan, 40000U, 40000U)) {
    return 8;
  }

  plan = processRepeated(engine, kScore30, kScore30, 2U, nextFrameId);
  if (!hasGreenTimes(plan, 30000U, 30000U)) {
    return 9;
  }

  plan = processRepeated(engine, kScore20, kScore20, 2U, nextFrameId);
  if (!hasGreenTimes(plan, 20000U, 20000U)) {
    return 10;
  }

  TrafficSnapshot emergency = makeSnapshot(nextFrameId++, kScore90, kScore20);
  emergency.emergencyNorth = true;
  const TimingPlan northEmergency = engine.processTrafficMetrics(emergency);
  if (!northEmergency.emergencyNorthSouth || northEmergency.emergencyEastWest ||
      !hasGreenTimes(northEmergency, 20000U, 20000U)) {
    return 11;
  }

  emergency = makeSnapshot(nextFrameId++, kScore20, kScore90);
  emergency.emergencyEast = true;
  const TimingPlan eastEmergency = engine.processTrafficMetrics(emergency);
  if (eastEmergency.emergencyNorthSouth || !eastEmergency.emergencyEastWest ||
      !hasGreenTimes(eastEmergency, 20000U, 20000U)) {
    return 12;
  }

  plan = processRepeated(engine, kScore30, kScore30, 2U, nextFrameId);
  if (!hasGreenTimes(plan, 30000U, 30000U) || plan.emergencyNorthSouth ||
      plan.emergencyEastWest) {
    return 13;
  }

  TrafficDataReceiver receiver{};
  TrafficSnapshot invalid = makeSnapshot(nextFrameId++, kScore20, kScore20);
  invalid.timestampUs = common::monotonicNanoseconds() / 1000ULL;
  invalid.occupancyNorth = 1.20F;
  if (receiver.validateSnapshot(invalid)) {
    return 14;
  }

  TrafficSnapshot recovered = makeSnapshot(nextFrameId++, kScore30, kScore30);
  recovered.timestampUs = common::monotonicNanoseconds() / 1000ULL;
  if (!receiver.validateSnapshot(recovered) ||
      receiver.getLatestSnapshot().frameId != recovered.frameId) {
    return 15;
  }

  ConstraintManager constraints{};
  TimingPlan oversized{};
  oversized.greenNorthSouthMs = 60000U;
  oversized.greenEastWestMs = 60000U;
  const TimingPlan bounded = constraints.applyTimingConstraints(oversized);
  if (bounded.cycleLengthMs != 100000U ||
      !hasGreenTimes(bounded, 46000U, 46000U)) {
    return 16;
  }

  return 0;
}
