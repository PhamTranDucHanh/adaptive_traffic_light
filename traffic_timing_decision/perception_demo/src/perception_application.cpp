#include "perception_application.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <score/stop_token.hpp>
#include <string_view>

#include "common.h"
#include "perception_logger.h"

namespace {

constexpr std::uint32_t kPeriodMs = 3000U;

}  // namespace

namespace perception_demo {

PerceptionApplication::PerceptionApplication()
    : publisher_{traffic_ipc::kTrafficSnapshotQueueName,
                 traffic_ipc::kTrafficSnapshotLockName} {}

std::int32_t PerceptionApplication::Initialize(
    const score::mw::lifecycle::ApplicationContext& context) {
  (void)context;

  // Lifecycle starts this publisher before its downstream consumer. Resetting
  // the owned objects here is therefore safe and clears stale data or a named
  // semaphore left locked by a process killed during the previous Run Target.
  const auto queueStatus = publisher_.open(true);
  if (queueStatus != traffic_ipc::QueueStatus::kSuccess) {
    applicationLogger().LogError()
        << "[INIT][IPC] queue=" << traffic_ipc::kTrafficSnapshotQueueName
        << "; status="
        << std::string_view{traffic_ipc::queueStatusName(queueStatus)}
        << "; errno=" << publisher_.lastError();
    return EXIT_FAILURE;
  }
  if (!periodicWait_.valid()) {
    applicationLogger().LogError()
        << "[INIT][PERIODIC] condition variable initialization failed";
    publisher_.close();
    return EXIT_FAILURE;
  }

  nextFrameId_ = 1U;
  initialized_ = true;
  applicationLogger().LogInfo()
      << "[INIT] ready; period_ms=" << kPeriodMs
      << "; queue=" << traffic_ipc::kTrafficSnapshotQueueName
      << "; lifecycle_profile=Reporting";
  return EXIT_SUCCESS;
}

std::int32_t PerceptionApplication::Run(
    const score::cpp::stop_token& stopToken) {
  if (!initialized_) {
    return EXIT_FAILURE;
  }

  timespec nextRelease{};
  if (!common::monotonicNow(nextRelease)) {
    shutdown();
    return EXIT_FAILURE;
  }
  common::addMilliseconds(nextRelease, kPeriodMs);

  std::int32_t exitCode{EXIT_SUCCESS};
  score::cpp::stop_callback stopWake{
      stopToken, [this]() noexcept { periodicWait_.requestStop(); }};
  applicationLogger().LogInfo()
      << "[RUN] periodic producer started; clock=CLOCK_MONOTONIC; "
         "wait=pthread_cond_timedwait; deadline=absolute";
  while (!stopToken.stop_requested()) {
    const int sleepResult = periodicWait_.waitUntil(nextRelease);
    if (sleepResult == ECANCELED || stopToken.stop_requested()) {
      break;
    }
    if (sleepResult != 0) {
      applicationLogger().LogError()
          << "[RUN] periodic wait failed: "
          << std::string_view{std::strerror(sleepResult)};
      exitCode = EXIT_FAILURE;
      break;
    }

    const auto snapshot = makeSnapshot();
    const auto publishStatus = publisher_.publish(snapshot);
    if (publishStatus == traffic_ipc::QueueStatus::kSuccess) {
      const std::uint32_t northSouthVehicleTotal =
          snapshot.vehicleCountNorth + snapshot.vehicleCountSouth;
      const std::uint32_t eastWestVehicleTotal =
          snapshot.vehicleCountEast + snapshot.vehicleCountWest;
      const float northSouthVehicleAverage =
          static_cast<float>(northSouthVehicleTotal) * 0.5F;
      const float eastWestVehicleAverage =
          static_cast<float>(eastWestVehicleTotal) * 0.5F;
      const float northSouthQueueAverage =
          (snapshot.queueLengthNorth + snapshot.queueLengthSouth) * 0.5F;
      const float eastWestQueueAverage =
          (snapshot.queueLengthEast + snapshot.queueLengthWest) * 0.5F;
      const float northSouthOccupancyAveragePercent =
          (snapshot.occupancyNorth + snapshot.occupancySouth) * 50.0F;
      const float eastWestOccupancyAveragePercent =
          (snapshot.occupancyEast + snapshot.occupancyWest) * 50.0F;
      const bool northSouthEmergency =
          snapshot.emergencyNorth || snapshot.emergencySouth;
      const bool eastWestEmergency =
          snapshot.emergencyEast || snapshot.emergencyWest;

      applicationLogger().LogInfo()
          << "[IPC][SNAPSHOT][PUBLISHED] frame_id=" << snapshot.frameId
          << "; timestamp_us=" << snapshot.timestampUs;
      applicationLogger().LogInfo()
          << "[IPC][SNAPSHOT][NS] north_vehicles=" << snapshot.vehicleCountNorth
          << "; south_vehicles=" << snapshot.vehicleCountSouth
          << "; vehicle_total=" << northSouthVehicleTotal
          << "; vehicle_avg=" << northSouthVehicleAverage
          << "; north_queue=" << snapshot.queueLengthNorth
          << "; south_queue=" << snapshot.queueLengthSouth
          << "; queue_avg=" << northSouthQueueAverage
          << "; north_occupancy_pct=" << snapshot.occupancyNorth * 100.0F
          << "; south_occupancy_pct=" << snapshot.occupancySouth * 100.0F
          << "; occupancy_avg_pct=" << northSouthOccupancyAveragePercent
          << "; emergency_north=" << snapshot.emergencyNorth
          << "; emergency_south=" << snapshot.emergencySouth
          << "; emergency_ns=" << northSouthEmergency;
      applicationLogger().LogInfo()
          << "[IPC][SNAPSHOT][EW] east_vehicles=" << snapshot.vehicleCountEast
          << "; west_vehicles=" << snapshot.vehicleCountWest
          << "; vehicle_total=" << eastWestVehicleTotal
          << "; vehicle_avg=" << eastWestVehicleAverage
          << "; east_queue=" << snapshot.queueLengthEast
          << "; west_queue=" << snapshot.queueLengthWest
          << "; queue_avg=" << eastWestQueueAverage
          << "; east_occupancy_pct=" << snapshot.occupancyEast * 100.0F
          << "; west_occupancy_pct=" << snapshot.occupancyWest * 100.0F
          << "; occupancy_avg_pct=" << eastWestOccupancyAveragePercent
          << "; emergency_east=" << snapshot.emergencyEast
          << "; emergency_west=" << snapshot.emergencyWest
          << "; emergency_ew=" << eastWestEmergency;
    } else if (publishStatus == traffic_ipc::QueueStatus::kDeferred) {
      applicationLogger().LogWarn()
          << "[IPC][SNAPSHOT][DEFERRED] frame_id=" << snapshot.frameId
          << "; reason=shared_lock_busy; retry=next_cycle";
    } else {
      applicationLogger().LogError()
          << "[IPC][SNAPSHOT][SEND] status="
          << std::string_view{traffic_ipc::queueStatusName(publishStatus)}
          << "; errno=" << publisher_.lastError();
      exitCode = EXIT_FAILURE;
    }

    if (exitCode != EXIT_SUCCESS) {
      break;
    }

    common::addMilliseconds(nextRelease, kPeriodMs);
    timespec now{};
    if (common::monotonicNow(now)) {
      const auto skipped = common::advancePastNow(nextRelease, kPeriodMs, now);
      if (skipped > 0U) {
        applicationLogger().LogWarn()
            << "[RUN][OVERRUN] skipped_releases=" << skipped;
      }
    }
  }

  shutdown();
  return exitCode;
}

traffic_ipc::TrafficSnapshot PerceptionApplication::makeSnapshot() {
  traffic_ipc::TrafficSnapshot snapshot{};
  snapshot.frameId = nextFrameId_++;
  snapshot.timestampUs = common::monotonicNanoseconds() / 1000ULL;

  const std::uint32_t phase = static_cast<std::uint32_t>(snapshot.frameId % 8U);
  snapshot.vehicleCountNorth = 10U + phase * 3U;
  snapshot.vehicleCountSouth = 8U + phase * 2U;
  snapshot.vehicleCountEast = 22U - phase;
  snapshot.vehicleCountWest = 18U - phase;
  snapshot.queueLengthNorth = 8.0F + static_cast<float>(phase) * 5.0F;
  snapshot.queueLengthSouth = 6.0F + static_cast<float>(phase) * 4.0F;
  snapshot.queueLengthEast = 38.0F - static_cast<float>(phase) * 3.0F;
  snapshot.queueLengthWest = 30.0F - static_cast<float>(phase) * 2.0F;
  snapshot.occupancyNorth = 0.20F + static_cast<float>(phase) * 0.06F;
  snapshot.occupancySouth = 0.18F + static_cast<float>(phase) * 0.05F;
  snapshot.occupancyEast = 0.70F - static_cast<float>(phase) * 0.05F;
  snapshot.occupancyWest = 0.65F - static_cast<float>(phase) * 0.04F;

  // A deterministic event periodically exercises the emergency branch of the
  // Timing Decision flow without changing the IPC contract.
  snapshot.emergencyNorth = snapshot.frameId % 10U == 0U;
  return snapshot;
}

void PerceptionApplication::shutdown() {
  if (!initialized_) {
    return;
  }
  publisher_.close();
  initialized_ = false;
  applicationLogger().LogInfo()
      << "[STOP] published_frames=" << nextFrameId_ - 1U;
}

}  // namespace perception_demo
