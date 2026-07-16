#ifndef DECISION_TYPES_H
#define DECISION_TYPES_H

#include <cstdint>

//============================================================
// Traffic Snapshot
//============================================================
struct TrafficSnapshot {
  uint64_t frameId{0};
  uint64_t timestampUs{0};

  uint32_t vehicleCountNorth{0};
  uint32_t vehicleCountSouth{0};
  uint32_t vehicleCountEast{0};
  uint32_t vehicleCountWest{0};

  float queueLengthNorth{0.0F};
  float queueLengthSouth{0.0F};
  float queueLengthEast{0.0F};
  float queueLengthWest{0.0F};

  float occupancyNorth{0.0F};
  float occupancySouth{0.0F};
  float occupancyEast{0.0F};
  float occupancyWest{0.0F};

  bool emergencyNorthSouth{false};
  bool emergencyEastWest{false};
};

//============================================================
// Demand Score
//============================================================
struct DemandScore {
  float northSouthScore{0.0F};
  float eastWestScore{0.0F};
  float overallScore{0.0F};
};

//============================================================
// Timing Plan
//============================================================
struct TimingPlan {
  uint64_t planId{0};
  uint64_t generationTimestampNs{0};

  uint32_t greenNorthSouthMs{0};
  uint32_t greenEastWestMs{0};
  uint32_t yellowMs{3000};
  uint32_t allRedMs{1000};
  uint32_t cycleLengthMs{0};

  bool emergencyNorthSouth{false};
  bool emergencyEastWest{false};
};

//============================================================
// Health Report
//============================================================
struct HealthReport {
  uint64_t cycleStartTimestampNs{0};
  uint64_t cycleFinishTimestampNs{0};
  uint64_t executionTimeNs{0};
  bool deadlineMissed{false};
  bool heartbeat{true};
};

#endif  // !DECISION_TYPES_H