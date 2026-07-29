#ifndef TRAFFIC_IPC_MESSAGES_H
#define TRAFFIC_IPC_MESSAGES_H

#include <cstdint>
#include <type_traits>

namespace traffic_ipc {

inline constexpr char kTrafficSnapshotQueueName[] = "/traffic_snapshot_v1";
inline constexpr char kTrafficSnapshotLockName[] =
    "/traffic_snapshot_lock_v1";
inline constexpr char kTimingPlanQueueName[] = "/timing_plan_v1";
inline constexpr char kTimingPlanLockName[] = "/timing_plan_lock_v1";

struct TrafficSnapshot {
  std::uint64_t frameId{0U};
  std::uint64_t timestampUs{0U};

  std::uint32_t vehicleCountNorth{0U};
  std::uint32_t vehicleCountSouth{0U};
  std::uint32_t vehicleCountEast{0U};
  std::uint32_t vehicleCountWest{0U};

  float queueLengthNorth{0.0F};
  float queueLengthSouth{0.0F};
  float queueLengthEast{0.0F};
  float queueLengthWest{0.0F};

  float occupancyNorth{0.0F};
  float occupancySouth{0.0F};
  float occupancyEast{0.0F};
  float occupancyWest{0.0F};

  bool emergencyNorth{false};
  bool emergencySouth{false};
  bool emergencyEast{false};
  bool emergencyWest{false};
};

struct TimingPlan {
  std::uint64_t planId{0U};
  std::uint64_t generationTimestampNs{0U};

  std::uint32_t greenNorthSouthMs{0U};
  std::uint32_t greenEastWestMs{0U};
  std::uint32_t yellowMs{3000U};
  std::uint32_t allRedMs{1000U};
  std::uint32_t cycleLengthMs{0U};

  bool emergencyNorthSouth{false};
  bool emergencyEastWest{false};
};

static_assert(std::is_trivially_copyable<TrafficSnapshot>::value,
              "TrafficSnapshot must be transferable as POSIX MQ bytes");
static_assert(std::is_trivially_copyable<TimingPlan>::value,
              "TimingPlan must be transferable as POSIX MQ bytes");

}  // namespace traffic_ipc

#endif  // TRAFFIC_IPC_MESSAGES_H
