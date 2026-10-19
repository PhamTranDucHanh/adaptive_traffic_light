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

enum class TrafficProfileVehicleCount : std::uint32_t {
  kScore20 = 20U,
  kScore30 = 40U,
  kScore60 = 60U,
  kScore90 = 80U,
};

enum class DemoRepeatCount : std::uint32_t {
  kTwoCycles = 2U,
  kFourCycles = 4U,
  kSixCycles = 6U,
};

enum class DemoFrameIdentifier : std::uint64_t {
  kFirst = 1U,
};

enum class DemoTimeConversion : std::uint64_t {
  kNanosecondsPerMicrosecond = 1000ULL,
};

enum class ExpectedTimingMilliseconds : std::uint32_t {
  kProportionallyScaledGreen = 46000U,
};

enum class DemoExitCode : std::int32_t {
  kSuccess = 0,
  kLowDemandTargetMismatch = 1,
  kModerateDemandTargetMismatch = 2,
  kHighDemandTargetMismatch = 3,
  kNorthSouthBusyMismatch = 4,
  kEastWestBusyMismatch = 5,
  kBalancedRecoveryMismatch = 6,
  kProportionalReductionMismatch = 7,
  kHighDemandDecreaseMismatch = 8,
  kModerateDemandDecreaseMismatch = 9,
  kLowDemandDecreaseMismatch = 10,
  kNorthSouthEmergencyMismatch = 11,
  kEastWestEmergencyMismatch = 12,
  kEmergencyResetMismatch = 13,
  kInvalidSnapshotAccepted = 14,
  kSnapshotRecoveryMismatch = 15,
  kDirectConstraintMismatch = 16,
};

constexpr float kScore20QueueLength = 20.0F;
constexpr float kScore20Occupancy = 0.20F;
constexpr float kScore30QueueLength = 20.0F;
constexpr float kScore30Occupancy = 0.40F;
constexpr float kScore60QueueLength = 60.0F;
constexpr float kScore60Occupancy = 0.60F;
constexpr float kScore90QueueLength = 100.0F;
constexpr float kScore90Occupancy = 0.80F;
constexpr float kInvalidOccupancy = 1.20F;

constexpr std::uint32_t toVehicleCount(
    const TrafficProfileVehicleCount value) noexcept {
  return static_cast<std::uint32_t>(value);
}

constexpr std::uint32_t toRepeatCount(const DemoRepeatCount value) noexcept {
  return static_cast<std::uint32_t>(value);
}

constexpr std::uint64_t toFrameIdentifier(
    const DemoFrameIdentifier value) noexcept {
  return static_cast<std::uint64_t>(value);
}

constexpr std::uint64_t toTimeConversion(
    const DemoTimeConversion value) noexcept {
  return static_cast<std::uint64_t>(value);
}

constexpr std::uint32_t toExpectedMilliseconds(
    const ExpectedTimingMilliseconds value) noexcept {
  return static_cast<std::uint32_t>(value);
}

constexpr std::int32_t toExitCode(const DemoExitCode value) noexcept {
  return static_cast<std::int32_t>(value);
}

constexpr DirectionTraffic kScore20{
    toVehicleCount(TrafficProfileVehicleCount::kScore20), kScore20QueueLength,
    kScore20Occupancy};
constexpr DirectionTraffic kScore30{
    toVehicleCount(TrafficProfileVehicleCount::kScore30), kScore30QueueLength,
    kScore30Occupancy};
constexpr DirectionTraffic kScore60{
    toVehicleCount(TrafficProfileVehicleCount::kScore60), kScore60QueueLength,
    kScore60Occupancy};
constexpr DirectionTraffic kScore90{
    toVehicleCount(TrafficProfileVehicleCount::kScore90), kScore90QueueLength,
    kScore90Occupancy};

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
  for (std::uint32_t index{}; index < repeatCount; ++index) {
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

int32_t main() {
  DecisionEngine engine{};
  std::uint64_t nextFrameId{toFrameIdentifier(DemoFrameIdentifier::kFirst)};

  // Exercise every demand bucket and both adjustment directions. The repeat
  // counts mirror perception_demo and account for the five-second step limit.
  TimingPlan plan =
      processRepeated(engine, kScore20, kScore20,
                      toRepeatCount(DemoRepeatCount::kTwoCycles), nextFrameId);
  if (!hasGreenTimes(
          plan,
          traffic_timing_decision::toMilliseconds(
              traffic_timing_decision::TimingMilliseconds::kLowDemandGreen),
          traffic_timing_decision::toMilliseconds(
              traffic_timing_decision::TimingMilliseconds::kLowDemandGreen))) {
    return toExitCode(DemoExitCode::kLowDemandTargetMismatch);
  }

  plan =
      processRepeated(engine, kScore30, kScore30,
                      toRepeatCount(DemoRepeatCount::kTwoCycles), nextFrameId);
  if (!hasGreenTimes(plan,
                     traffic_timing_decision::toMilliseconds(
                         traffic_timing_decision::TimingMilliseconds::
                             kModerateDemandGreen),
                     traffic_timing_decision::toMilliseconds(
                         traffic_timing_decision::TimingMilliseconds::
                             kModerateDemandGreen))) {
    return toExitCode(DemoExitCode::kModerateDemandTargetMismatch);
  }

  plan =
      processRepeated(engine, kScore60, kScore60,
                      toRepeatCount(DemoRepeatCount::kTwoCycles), nextFrameId);
  if (!hasGreenTimes(
          plan,
          traffic_timing_decision::toMilliseconds(
              traffic_timing_decision::TimingMilliseconds::kHighDemandGreen),
          traffic_timing_decision::toMilliseconds(
              traffic_timing_decision::TimingMilliseconds::kHighDemandGreen))) {
    return toExitCode(DemoExitCode::kHighDemandTargetMismatch);
  }

  plan =
      processRepeated(engine, kScore90, kScore20,
                      toRepeatCount(DemoRepeatCount::kFourCycles), nextFrameId);
  if (!hasGreenTimes(
          plan,
          traffic_timing_decision::toMilliseconds(
              traffic_timing_decision::TimingMilliseconds::
                  kVeryHighDemandGreen),
          traffic_timing_decision::toMilliseconds(
              traffic_timing_decision::TimingMilliseconds::kLowDemandGreen))) {
    return toExitCode(DemoExitCode::kNorthSouthBusyMismatch);
  }

  plan =
      processRepeated(engine, kScore20, kScore90,
                      toRepeatCount(DemoRepeatCount::kSixCycles), nextFrameId);
  if (!hasGreenTimes(
          plan,
          traffic_timing_decision::toMilliseconds(
              traffic_timing_decision::TimingMilliseconds::kLowDemandGreen),
          traffic_timing_decision::toMilliseconds(
              traffic_timing_decision::TimingMilliseconds::
                  kVeryHighDemandGreen))) {
    return toExitCode(DemoExitCode::kEastWestBusyMismatch);
  }

  plan =
      processRepeated(engine, kScore30, kScore30,
                      toRepeatCount(DemoRepeatCount::kFourCycles), nextFrameId);
  if (!hasGreenTimes(plan,
                     traffic_timing_decision::toMilliseconds(
                         traffic_timing_decision::TimingMilliseconds::
                             kModerateDemandGreen),
                     traffic_timing_decision::toMilliseconds(
                         traffic_timing_decision::TimingMilliseconds::
                             kModerateDemandGreen))) {
    return toExitCode(DemoExitCode::kBalancedRecoveryMismatch);
  }

  // Both targets are 50 s. Their 108 s draft cycle exceeds the 100 s cap, so
  // 92 s of available green is divided proportionally as 46 s / 46 s.
  plan =
      processRepeated(engine, kScore90, kScore90,
                      toRepeatCount(DemoRepeatCount::kFourCycles), nextFrameId);
  const std::uint32_t proportionallyScaledGreen = toExpectedMilliseconds(
      ExpectedTimingMilliseconds::kProportionallyScaledGreen);
  if (!hasGreenTimes(plan, proportionallyScaledGreen,
                     proportionallyScaledGreen) ||
      plan.cycleLengthMs !=
          traffic_timing_decision::toMilliseconds(
              traffic_timing_decision::TimingMilliseconds::kMaximumCycle)) {
    return toExitCode(DemoExitCode::kProportionalReductionMismatch);
  }

  plan =
      processRepeated(engine, kScore60, kScore60,
                      toRepeatCount(DemoRepeatCount::kTwoCycles), nextFrameId);
  if (!hasGreenTimes(
          plan,
          traffic_timing_decision::toMilliseconds(
              traffic_timing_decision::TimingMilliseconds::kHighDemandGreen),
          traffic_timing_decision::toMilliseconds(
              traffic_timing_decision::TimingMilliseconds::kHighDemandGreen))) {
    return toExitCode(DemoExitCode::kHighDemandDecreaseMismatch);
  }

  plan =
      processRepeated(engine, kScore30, kScore30,
                      toRepeatCount(DemoRepeatCount::kTwoCycles), nextFrameId);
  if (!hasGreenTimes(plan,
                     traffic_timing_decision::toMilliseconds(
                         traffic_timing_decision::TimingMilliseconds::
                             kModerateDemandGreen),
                     traffic_timing_decision::toMilliseconds(
                         traffic_timing_decision::TimingMilliseconds::
                             kModerateDemandGreen))) {
    return toExitCode(DemoExitCode::kModerateDemandDecreaseMismatch);
  }

  plan =
      processRepeated(engine, kScore20, kScore20,
                      toRepeatCount(DemoRepeatCount::kTwoCycles), nextFrameId);
  if (!hasGreenTimes(
          plan,
          traffic_timing_decision::toMilliseconds(
              traffic_timing_decision::TimingMilliseconds::kLowDemandGreen),
          traffic_timing_decision::toMilliseconds(
              traffic_timing_decision::TimingMilliseconds::kLowDemandGreen))) {
    return toExitCode(DemoExitCode::kLowDemandDecreaseMismatch);
  }

  TrafficSnapshot emergency = makeSnapshot(nextFrameId++, kScore90, kScore20);
  emergency.emergencyNorth = true;
  const TimingPlan northEmergency = engine.processTrafficMetrics(emergency);
  if (!northEmergency.emergencyNorthSouth || northEmergency.emergencyEastWest ||
      !hasGreenTimes(
          northEmergency,
          traffic_timing_decision::toMilliseconds(
              traffic_timing_decision::TimingMilliseconds::kLowDemandGreen),
          traffic_timing_decision::toMilliseconds(
              traffic_timing_decision::TimingMilliseconds::kLowDemandGreen))) {
    return toExitCode(DemoExitCode::kNorthSouthEmergencyMismatch);
  }

  emergency = makeSnapshot(nextFrameId++, kScore20, kScore90);
  emergency.emergencyEast = true;
  const TimingPlan eastEmergency = engine.processTrafficMetrics(emergency);
  if (eastEmergency.emergencyNorthSouth || !eastEmergency.emergencyEastWest ||
      !hasGreenTimes(
          eastEmergency,
          traffic_timing_decision::toMilliseconds(
              traffic_timing_decision::TimingMilliseconds::kLowDemandGreen),
          traffic_timing_decision::toMilliseconds(
              traffic_timing_decision::TimingMilliseconds::kLowDemandGreen))) {
    return toExitCode(DemoExitCode::kEastWestEmergencyMismatch);
  }

  plan =
      processRepeated(engine, kScore30, kScore30,
                      toRepeatCount(DemoRepeatCount::kTwoCycles), nextFrameId);
  if (!hasGreenTimes(plan,
                     traffic_timing_decision::toMilliseconds(
                         traffic_timing_decision::TimingMilliseconds::
                             kModerateDemandGreen),
                     traffic_timing_decision::toMilliseconds(
                         traffic_timing_decision::TimingMilliseconds::
                             kModerateDemandGreen)) ||
      plan.emergencyNorthSouth || plan.emergencyEastWest) {
    return toExitCode(DemoExitCode::kEmergencyResetMismatch);
  }

  TrafficDataReceiver receiver{};
  TrafficSnapshot invalid = makeSnapshot(nextFrameId++, kScore20, kScore20);
  invalid.timestampUs =
      common::monotonicNanoseconds() /
      toTimeConversion(DemoTimeConversion::kNanosecondsPerMicrosecond);
  invalid.occupancyNorth = kInvalidOccupancy;
  if (receiver.validateSnapshot(invalid)) {
    return toExitCode(DemoExitCode::kInvalidSnapshotAccepted);
  }

  TrafficSnapshot recovered = makeSnapshot(nextFrameId++, kScore30, kScore30);
  recovered.timestampUs =
      common::monotonicNanoseconds() /
      toTimeConversion(DemoTimeConversion::kNanosecondsPerMicrosecond);
  if (!receiver.validateSnapshot(recovered) ||
      receiver.getLatestSnapshot().frameId != recovered.frameId) {
    return toExitCode(DemoExitCode::kSnapshotRecoveryMismatch);
  }

  ConstraintManager constraints{};
  TimingPlan oversized{};
  oversized.greenNorthSouthMs = traffic_timing_decision::toMilliseconds(
      traffic_timing_decision::TimingMilliseconds::kMaximumGreen);
  oversized.greenEastWestMs = traffic_timing_decision::toMilliseconds(
      traffic_timing_decision::TimingMilliseconds::kMaximumGreen);
  const TimingPlan bounded = constraints.applyTimingConstraints(oversized);
  if (bounded.cycleLengthMs !=
          traffic_timing_decision::toMilliseconds(
              traffic_timing_decision::TimingMilliseconds::kMaximumCycle) ||
      !hasGreenTimes(bounded, proportionallyScaledGreen,
                     proportionallyScaledGreen)) {
    return toExitCode(DemoExitCode::kDirectConstraintMismatch);
  }

  return toExitCode(DemoExitCode::kSuccess);
}
