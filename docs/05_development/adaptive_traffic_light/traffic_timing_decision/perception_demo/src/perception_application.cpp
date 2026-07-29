#include "perception_application.h"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <score/stop_token.hpp>
#include <string_view>

#include "common.h"
#include "perception_logger.h"

namespace {

constexpr std::uint32_t kPeriodMs = 3000U;
constexpr std::uint32_t kTargetGreen20sMs = 20000U;
constexpr std::uint32_t kTargetGreen30sMs = 30000U;
constexpr std::uint32_t kTargetGreen40sMs = 40000U;
constexpr std::uint32_t kTargetGreen50sMs = 50000U;

struct DirectionTraffic {
  std::uint32_t vehiclesPerApproach;
  float queueLengthPerApproach;
  float occupancyPerApproach;
};

struct TrafficScenario {
  std::uint32_t id;
  std::string_view name;
  std::string_view description;
  DirectionTraffic northSouth;
  DirectionTraffic eastWest;
  std::uint32_t repeatFrames;
  bool emergencyNorth;
  bool emergencySouth;
  bool emergencyEast;
  bool emergencyWest;
  bool expectedValid;
  std::string_view invalidReason;
};

constexpr DirectionTraffic kLightTraffic{20U, 20.0F, 0.20F};
constexpr DirectionTraffic kTarget30BoundaryTraffic{40U, 20.0F, 0.40F};
constexpr DirectionTraffic kTarget40BoundaryTraffic{60U, 60.0F, 0.60F};
constexpr DirectionTraffic kTarget50BoundaryTraffic{80U, 100.0F, 0.80F};
constexpr DirectionTraffic kInvalidOccupancyTraffic{20U, 20.0F, 1.20F};

constexpr float demandScore(const DirectionTraffic& traffic) noexcept {
  return 0.5F * traffic.queueLengthPerApproach +
         0.3F * static_cast<float>(traffic.vehiclesPerApproach) +
         0.2F * (traffic.occupancyPerApproach * 100.0F);
}

constexpr bool approximatelyEqual(const float first, const float second,
                                  const float tolerance = 0.001F) noexcept {
  return first >= second - tolerance && first <= second + tolerance;
}

constexpr std::uint32_t expectedTargetGreenMs(const float score) noexcept {
  if (score < 30.0F) {
    return kTargetGreen20sMs;
  }
  if (score < 60.0F) {
    return kTargetGreen30sMs;
  }
  if (score < 90.0F) {
    return kTargetGreen40sMs;
  }
  return kTargetGreen50sMs;
}

static_assert(approximatelyEqual(demandScore(kLightTraffic), 20.0F),
              "Light traffic must produce demand score 20");
static_assert(approximatelyEqual(demandScore(kTarget30BoundaryTraffic), 30.0F),
              "Target-30 boundary traffic must produce demand score 30");
static_assert(approximatelyEqual(demandScore(kTarget40BoundaryTraffic), 60.0F),
              "Target-40 boundary traffic must produce demand score 60");
static_assert(approximatelyEqual(demandScore(kTarget50BoundaryTraffic), 90.0F),
              "Target-50 boundary traffic must produce demand score 90");

// Repeat counts are intentional: one accepted snapshot moves each green time
// by at most five seconds. The sequence starts from the engine's 30 s / 30 s
// plan and leaves every scenario at the state described in its log message.
constexpr std::array<TrafficScenario, 14U> kScenarios{{
    {1U, "balanced_light",
     "Both directions are light (score 20): decrease 30 s -> 20 s in two "
     "5 s steps.",
     kLightTraffic, kLightTraffic, 2U, false, false, false, false, true, ""},
    {2U, "balanced_score_30_boundary",
     "Both scores are exactly 30: exercise the <30 boundary and increase 20 "
     "s -> 30 s.",
     kTarget30BoundaryTraffic, kTarget30BoundaryTraffic, 2U, false, false,
     false, false, true, ""},
    {3U, "balanced_score_60_boundary",
     "Both scores are exactly 60: exercise the <60 boundary and increase 30 "
     "s -> 40 s.",
     kTarget40BoundaryTraffic, kTarget40BoundaryTraffic, 2U, false, false,
     false, false, true, ""},
    {4U, "ns_busy_ew_light",
     "NS is saturated while EW is light: increase NS 40 s -> 50 s and "
     "decrease EW 40 s -> 20 s.",
     kTarget50BoundaryTraffic, kLightTraffic, 4U, false, false, false, false,
     true, ""},
    {5U, "ew_busy_ns_light",
     "EW is saturated while NS is light: reverse the split from 50/20 s to "
     "20/50 s.",
     kLightTraffic, kTarget50BoundaryTraffic, 6U, false, false, false, false,
     true, ""},
    {6U, "balanced_recovery_to_30",
     "Both directions request 30 s: rebalance the asymmetric 20/50 s plan "
     "to 30/30 s.",
     kTarget30BoundaryTraffic, kTarget30BoundaryTraffic, 4U, false, false,
     false, false, true, ""},
    {7U, "both_saturated_cycle_cap",
     "Both directions request 50 s. The 50/50 s draft makes a 108 s cycle, "
     "so the 100 s maximum scales green proportionally to 46/46 s.",
     kTarget50BoundaryTraffic, kTarget50BoundaryTraffic, 4U, false, false,
     false, false, true, ""},
    {8U, "both_heavy_reduce_to_40",
     "After proportional scaling, both directions request 40 s and decrease "
     "46 s -> 41 s -> 40 s.",
     kTarget40BoundaryTraffic, kTarget40BoundaryTraffic, 2U, false, false,
     false, false, true, ""},
    {9U, "both_medium_reduce_to_30",
     "Both directions request 30 s: decrease 40 s -> 30 s in two steps.",
     kTarget30BoundaryTraffic, kTarget30BoundaryTraffic, 2U, false, false,
     false, false, true, ""},
    {10U, "both_light_reduce_to_20",
     "Both directions request 20 s: decrease 30 s -> 20 s in two steps.",
     kLightTraffic, kLightTraffic, 2U, false, false, false, false, true, ""},
    {11U, "north_emergency",
     "North emergency is active: keep the previous green times and publish "
     "only the NS emergency flag.",
     kTarget50BoundaryTraffic, kLightTraffic, 1U, true, false, false, false,
     true, ""},
    {12U, "east_emergency",
     "East emergency is active: keep the previous green times, clear the NS "
     "flag and publish the EW emergency flag.",
     kLightTraffic, kTarget50BoundaryTraffic, 1U, false, false, true, false,
     true, ""},
    {13U, "invalid_occupancy_fail_safe",
     "North occupancy is 120%: receiver must reject the snapshot and retain "
     "the previous timing plan.",
     kInvalidOccupancyTraffic, kLightTraffic, 1U, false, false, false, false,
     false, "north_occupancy_above_1.0"},
    {14U, "valid_input_recovery",
     "Valid score-30 traffic resumes after rejection: process normally, "
     "clear emergency flags and finish at 30/30 s.",
     kTarget30BoundaryTraffic, kTarget30BoundaryTraffic, 2U, false, false,
     false, false, true, ""},
}};

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
  scenarioIndex_ = 0U;
  scenarioFrameIndex_ = 0U;
  completedScenarioSuites_ = 0U;
  scenarioStartLogged_ = false;
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

    if (!scenarioStartLogged_) {
      logCurrentScenario();
      scenarioStartLogged_ = true;
    }

    const auto snapshot = makeSnapshot();
    const auto publishStatus = publisher_.publish(snapshot);
    if (publishStatus == traffic_ipc::QueueStatus::kSuccess) {
      const auto& scenario = kScenarios[scenarioIndex_];
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
          << "[IPC][SNAPSHOT][PUBLISHED] scenario_id=" << scenario.id
          << "; scenario_name=" << scenario.name
          << "; scenario_frame=" << scenarioFrameIndex_ + 1U << "/"
          << scenario.repeatFrames << "; frame_id=" << snapshot.frameId
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
      advanceScenario();
    } else if (publishStatus == traffic_ipc::QueueStatus::kDeferred) {
      applicationLogger().LogWarn()
          << "[IPC][SNAPSHOT][DEFERRED] frame_id=" << snapshot.frameId
          << "; scenario_id=" << kScenarios[scenarioIndex_].id
          << "; reason=shared_lock_busy; retry=same_scenario_frame";
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
  snapshot.frameId = nextFrameId_;
  snapshot.timestampUs = common::monotonicNanoseconds() / 1000ULL;

  const auto& scenario = kScenarios[scenarioIndex_];
  snapshot.vehicleCountNorth = scenario.northSouth.vehiclesPerApproach;
  snapshot.vehicleCountSouth = scenario.northSouth.vehiclesPerApproach;
  snapshot.vehicleCountEast = scenario.eastWest.vehiclesPerApproach;
  snapshot.vehicleCountWest = scenario.eastWest.vehiclesPerApproach;
  snapshot.queueLengthNorth = scenario.northSouth.queueLengthPerApproach;
  snapshot.queueLengthSouth = scenario.northSouth.queueLengthPerApproach;
  snapshot.queueLengthEast = scenario.eastWest.queueLengthPerApproach;
  snapshot.queueLengthWest = scenario.eastWest.queueLengthPerApproach;
  snapshot.occupancyNorth = scenario.northSouth.occupancyPerApproach;
  snapshot.occupancySouth = scenario.northSouth.occupancyPerApproach;
  snapshot.occupancyEast = scenario.eastWest.occupancyPerApproach;
  snapshot.occupancyWest = scenario.eastWest.occupancyPerApproach;
  snapshot.emergencyNorth = scenario.emergencyNorth;
  snapshot.emergencySouth = scenario.emergencySouth;
  snapshot.emergencyEast = scenario.emergencyEast;
  snapshot.emergencyWest = scenario.emergencyWest;
  return snapshot;
}

void PerceptionApplication::logCurrentScenario() const {
  const auto& scenario = kScenarios[scenarioIndex_];
#ifdef SCENERIO_DETAIL
  applicationLogger().LogInfo()
      << "============================================================";
  applicationLogger().LogInfo()
      << "[SCENARIO][START] suite=" << completedScenarioSuites_ + 1U
      << "; scenario=" << scenario.id << "/" << kScenarios.size()
      << "; name=" << scenario.name
      << "; repeat_frames=" << scenario.repeatFrames;
  applicationLogger().LogInfo()
      << "[SCENARIO][PURPOSE] " << scenario.description;
  applicationLogger().LogInfo()
      << "[SCENARIO][INPUT] ns_vehicles_each="
      << scenario.northSouth.vehiclesPerApproach
      << "; ns_queue_each=" << scenario.northSouth.queueLengthPerApproach
      << "; ns_occupancy_pct="
      << scenario.northSouth.occupancyPerApproach * 100.0F
      << "; ew_vehicles_each=" << scenario.eastWest.vehiclesPerApproach
      << "; ew_queue_each=" << scenario.eastWest.queueLengthPerApproach
      << "; ew_occupancy_pct="
      << scenario.eastWest.occupancyPerApproach * 100.0F;
  if (!scenario.expectedValid) {
    applicationLogger().LogInfo()
        << "[SCENARIO][EXPECTED] receiver_validation=reject"
        << "; reason=" << scenario.invalidReason
        << "; action=keep_previous_timing_plan";
  } else if (scenario.emergencyNorth || scenario.emergencySouth ||
             scenario.emergencyEast || scenario.emergencyWest) {
    applicationLogger().LogInfo()
        << "[SCENARIO][EXPECTED] receiver_validation=accept"
        << "; decision_action=keep_previous_green_and_forward_emergency_flags"
        << "; emergency_ns="
        << (scenario.emergencyNorth || scenario.emergencySouth)
        << "; emergency_ew="
        << (scenario.emergencyEast || scenario.emergencyWest);
  } else {
    const float northSouthScore = demandScore(scenario.northSouth);
    const float eastWestScore = demandScore(scenario.eastWest);
    applicationLogger().LogInfo()
        << "[SCENARIO][EXPECTED] receiver_validation=accept"
        << "; ns_score=" << northSouthScore
        << "; ns_target_green_ms=" << expectedTargetGreenMs(northSouthScore)
        << "; ew_score=" << eastWestScore
        << "; ew_target_green_ms=" << expectedTargetGreenMs(eastWestScore)
        << "; decision_action=step_toward_targets";
  }
#endif
}

void PerceptionApplication::advanceScenario() {
  ++nextFrameId_;
  ++scenarioFrameIndex_;
  if (scenarioFrameIndex_ < kScenarios[scenarioIndex_].repeatFrames) {
    return;
  }

  scenarioFrameIndex_ = 0U;
  scenarioStartLogged_ = false;
  ++scenarioIndex_;
  if (scenarioIndex_ < kScenarios.size()) {
    return;
  }

  scenarioIndex_ = 0U;
  ++completedScenarioSuites_;
  applicationLogger().LogInfo()
      << "[SCENARIO][SUITE_COMPLETE] completed_suites="
      << completedScenarioSuites_ << "; published_frames=" << nextFrameId_ - 1U
      << "; action=restart_from_scenario_1";
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
