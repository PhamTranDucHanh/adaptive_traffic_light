#include "perception_application.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "perception_logger.h"
#include "common/absolute_periodic.h"

namespace {

constexpr std::uint32_t kPeriodMs = 3000U;

}  // namespace

namespace perception_demo {

PerceptionApplication::PerceptionApplication()
    : publisher_{traffic_ipc::kTrafficSnapshotQueueName,
                 traffic_ipc::kTrafficSnapshotLockName},
      healthReporter_{common::HealthProfile::kPerception} {}

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
  if (!healthReporter_.initialize()) {
    applicationLogger().LogError()
        << "[INIT][HEALTH] S-CORE HealthMonitor initialization failed";
    publisher_.close();
    return EXIT_FAILURE;
  }

  nextFrameId_ = 1U;
  initialized_ = true;
  applicationLogger().LogInfo()
      << "[INIT] ready; period_ms=" << kPeriodMs
      << "; queue=" << traffic_ipc::kTrafficSnapshotQueueName
      << "; health=enabled";
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
  applicationLogger().LogInfo()
      << "[RUN] periodic producer started; clock=CLOCK_MONOTONIC; "
         "sleep=TIMER_ABSTIME";
  while (!stopToken.stop_requested()) {
    const int sleepResult = common::sleepUntil(
        nextRelease, [&stopToken]() { return stopToken.stop_requested(); });
    if (stopToken.stop_requested()) {
      break;
    }
    if (sleepResult != 0) {
      applicationLogger().LogError()
          << "[RUN] clock_nanosleep failed: "
          << std::string_view{std::strerror(sleepResult)};
      exitCode = EXIT_FAILURE;
      break;
    }

    if (!healthReporter_.startCycle()) {
      applicationLogger().LogError() << "[HEALTH] could not start cycle";
      exitCode = EXIT_FAILURE;
      break;
    }

    const auto snapshot = makeSnapshot();
    const auto publishStatus = publisher_.publish(snapshot);
    if (publishStatus == traffic_ipc::QueueStatus::kSuccess) {
      applicationLogger().LogInfo()
          << "[IPC][SNAPSHOT][PUBLISHED] frame_id=" << snapshot.frameId
          << "; ns_vehicles="
          << snapshot.vehicleCountNorth + snapshot.vehicleCountSouth
          << "; ew_vehicles="
          << snapshot.vehicleCountEast + snapshot.vehicleCountWest
          << "; emergency="
          << (snapshot.emergencyNorth || snapshot.emergencySouth ||
              snapshot.emergencyEast || snapshot.emergencyWest);
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

    if (!healthReporter_.finishCycle()) {
      applicationLogger().LogError() << "[HEALTH] could not finish cycle";
      exitCode = EXIT_FAILURE;
    } else {
      applicationLogger().LogDebug()
          << "[HEALTH][CYCLE] count=" << healthReporter_.cycleCount()
          << "; elapsed_us=" << healthReporter_.lastCycleElapsedUs();
    }
    if (exitCode != EXIT_SUCCESS) {
      break;
    }

    common::addMilliseconds(nextRelease, kPeriodMs);
    timespec now{};
    if (common::monotonicNow(now)) {
      const auto skipped =
          common::advancePastNow(nextRelease, kPeriodMs, now);
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
  snapshot.timestampUs =
      common::monotonicNanoseconds() / 1000ULL;

  const std::uint32_t phase =
      static_cast<std::uint32_t>(snapshot.frameId % 8U);
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
  healthReporter_.shutdown();
  publisher_.close();
  initialized_ = false;
  applicationLogger().LogInfo()
      << "[STOP] published_frames=" << nextFrameId_ - 1U;
}

}  // namespace perception_demo
