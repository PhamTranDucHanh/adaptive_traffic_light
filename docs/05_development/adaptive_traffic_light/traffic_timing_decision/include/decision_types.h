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
  float northSouthScore{};
  float eastWestScore{};
  float overallScore{};
};

//============================================================
// Health Report
//============================================================
struct HealthReport {
  std::uint64_t cycleStartTimestampNs{};
  std::uint64_t cycleFinishTimestampNs{};
  std::uint64_t executionTimeNs{};
  bool deadlineMissed{false};
  bool heartbeat{true};
};

#endif  // !DECISION_TYPES_H
