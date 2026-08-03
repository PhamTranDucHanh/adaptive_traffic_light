#ifndef TRAFFIC_SIGNAL_CONTROLLER_COMMON_CONFIG_H_
#define TRAFFIC_SIGNAL_CONTROLLER_COMMON_CONFIG_H_

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

constexpr std::uint8_t MAX_PHASES{6U};
constexpr std::uint32_t TIMER_INTERVAL_MS{1'000U};

constexpr std::uint32_t kMillisecondsPerSecond{1'000U};
constexpr std::uint64_t kNanosecondsPerMicrosecond{1'000ULL};
constexpr std::uint64_t kNanosecondsPerMillisecond{1'000'000ULL};
constexpr std::uint64_t kNanosecondsPerSecond{1'000'000'000ULL};

constexpr std::uint32_t kMinGreenDurationMs{1'000U};
constexpr std::uint32_t kMaxGreenDurationMs{500'000U};
constexpr std::uint32_t kMinYellowDurationMs{1'000U};
constexpr std::uint32_t kMaxYellowDurationMs{10'000U};
constexpr std::uint32_t kMinAllRedDurationMs{500U};
constexpr std::uint32_t kMaxAllRedDurationMs{10'000U};

constexpr std::uint32_t kEmergencyLowerThresholdMs{2'000U};
constexpr std::uint32_t kEmergencyUpperThresholdMs{15'000U};
constexpr std::uint32_t kEmergencyGreenDurationMs{20'000U};

constexpr long kTimingPlanOpenRetryNanoseconds{100'000'000L};
constexpr long kTimingPlanReceiveTimeoutNanoseconds{200'000'000L};

constexpr const char* kDefaultAnalyticsLogFile{"/tmp/CTRL.dlt"};
constexpr const char* kDefaultAnalyticsReportFile{
    "/tmp/traffic_signal_controller/logs/analytics_report.txt"};
constexpr const char* kRuntimeDirectory{"/tmp/traffic_signal_controller"};
constexpr const char* kRuntimeLogDirectory{
    "/tmp/traffic_signal_controller/logs"};
constexpr const char* kRuntimeAnalyticsOutputDirectory{
    "/tmp/traffic_signal_controller/logs/output"};
constexpr unsigned int kRuntimeDirectoryPermissions{0755U};

constexpr std::int32_t kDefaultFsmPriority{80};
constexpr std::int32_t kDefaultPlanReceiverPriority{70};
// Keep the health monitor worker below the MQ receiver and FSM, but above the
// lifecycle/application thread. Without an explicit RT policy it can be starved
// by the real perception workload.
constexpr std::int32_t kHealthMonitorPriority{60};
constexpr std::int32_t kOutputSimulatorPriority{50};
constexpr std::int32_t kTrafficSignalControllerCpu{3};
constexpr std::int32_t kDecimalBase{10};

constexpr std::size_t kBytesPerKibibyte{1024U};
constexpr std::size_t kPrefaultStackSizeKibibytes{64U};
constexpr std::size_t kPrefaultStackBytes{kPrefaultStackSizeKibibytes *
                                          kBytesPerKibibyte};
constexpr std::size_t kPageSizeBytes{4096U};

// Deadline supervision covers every 1-second control cycle. Heartbeat
// supervision intentionally samples every second cycle: WSL2 can suspend the
// VM long enough for S-CORE v0.3.0 to observe two 1-second heartbeats in one
// evaluator pass and reject them as MultipleHeartbeats.
constexpr std::uint64_t kHeartbeatControlCycleInterval{2U};
constexpr auto kControlDeadlineMin = std::chrono::milliseconds{0U};
constexpr auto kControlDeadlineMax = std::chrono::milliseconds{1'000U};
constexpr auto kHeartbeatMin = std::chrono::milliseconds{500U};
constexpr auto kHeartbeatMax = std::chrono::milliseconds{5'000U};
constexpr auto kInternalProcessingCycle = std::chrono::milliseconds{100U};
constexpr auto kSupervisorApiCycle = std::chrono::milliseconds{500U};

constexpr std::uint64_t kControlPeriodMilliseconds{1'000U};

struct TimingPlan {
  std::uint64_t planId{0U};

  std::uint32_t greenNorthSouthMs{0U};
  std::uint32_t greenEastWestMs{0U};
  std::uint32_t yellowMs{3000U};
  std::uint32_t allRedMs{1000U};
  std::uint32_t cycleLengthMs{0U};

  bool emergencyNorthSouth{false};
  bool emergencyEastWest{false};
};

enum class PhaseId : std::uint8_t {
  NS_GREEN,
  YELLOW,
  ALL_RED,
  EW_GREEN,
};

struct Phase {
  PhaseId phaseId{PhaseId::ALL_RED};
  std::uint32_t durationMs{0U};
};

struct PlanData {
  std::uint64_t sourcePlanId{0U};
  std::array<Phase, MAX_PHASES> phases{};
  std::uint8_t phaseCount{0U};
  bool isEmergencyNS{false};
  bool isEmergencyEW{false};
};

inline PlanData MakeDefaultPlan() {
  PlanData plan{};

  plan.sourcePlanId = 0U;

  plan.phases[0] = Phase{PhaseId::NS_GREEN, 30'000U};

  plan.phases[1] = Phase{PhaseId::YELLOW, 3'000U};

  plan.phases[2] = Phase{PhaseId::ALL_RED, 1'000U};

  plan.phases[3] = Phase{PhaseId::EW_GREEN, 30'000U};

  plan.phases[4] = Phase{PhaseId::YELLOW, 3'000U};

  plan.phases[5] = Phase{PhaseId::ALL_RED, 1'000U};

  plan.phaseCount = 6U;

  plan.isEmergencyNS = false;
  plan.isEmergencyEW = false;

  return plan;
}

struct SignalDisplay {
  PhaseId phaseId{PhaseId::ALL_RED};
  std::uint32_t remainingTimeMs{0U};
};

#endif  // TRAFFIC_SIGNAL_CONTROLLER_COMMON_CONFIG_H_
