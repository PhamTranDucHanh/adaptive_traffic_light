#include "decision_engine.h"

int main() {
  DecisionEngine engine{};
  TrafficSnapshot snapshot{};
  snapshot.frameId = 1U;
  snapshot.timestampUs = 1U;
  snapshot.vehicleCountNorth = 10U;
  snapshot.vehicleCountSouth = 10U;
  snapshot.vehicleCountEast = 80U;
  snapshot.vehicleCountWest = 80U;
  snapshot.queueLengthNorth = 10.0F;
  snapshot.queueLengthSouth = 10.0F;
  snapshot.queueLengthEast = 80.0F;
  snapshot.queueLengthWest = 80.0F;
  snapshot.occupancyNorth = 0.2F;
  snapshot.occupancySouth = 0.2F;
  snapshot.occupancyEast = 0.8F;
  snapshot.occupancyWest = 0.8F;

  const TimingPlan normal = engine.processTrafficMetrics(snapshot);
  if (normal.planId != 1U || normal.greenNorthSouthMs != 25000U ||
      normal.greenEastWestMs != 35000U || normal.cycleLengthMs != 68000U ||
      normal.emergencyNorthSouth || normal.emergencyEastWest) {
    return 1;
  }

  snapshot.frameId = 2U;
  snapshot.emergencyNorth = true;
  const TimingPlan emergency = engine.processTrafficMetrics(snapshot);
  if (emergency.planId != 2U || !emergency.emergencyNorthSouth ||
      emergency.emergencyEastWest ||
      emergency.greenNorthSouthMs != normal.greenNorthSouthMs ||
      emergency.greenEastWestMs != normal.greenEastWestMs) {
    return 2;
  }

  ConstraintManager constraints{};
  TimingPlan oversized{};
  oversized.greenNorthSouthMs = 60000U;
  oversized.greenEastWestMs = 60000U;
  const TimingPlan bounded = constraints.applyTimingConstraints(oversized);
  if (bounded.cycleLengthMs != 120000U ||
      bounded.greenNorthSouthMs != 56000U ||
      bounded.greenEastWestMs != 56000U) {
    return 3;
  }
  return 0;
}
