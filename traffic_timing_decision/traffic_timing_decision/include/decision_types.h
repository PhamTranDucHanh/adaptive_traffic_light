#ifndef DECISION_TYPES_H
#define DECISION_TYPES_H

#include <cstdint>

#include "traffic_ipc/messages.h"

// Keep the module-facing names stable while the transport contract lives in
// the shared IPC package used by all three processes.
using TrafficSnapshot = traffic_ipc::TrafficSnapshot;
using TimingPlan = traffic_ipc::TimingPlan;

//============================================================
// Demand Score
//============================================================
struct DemandScore {
  float northSouthScore{0.0F};
  float eastWestScore{0.0F};
  float overallScore{0.0F};
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
