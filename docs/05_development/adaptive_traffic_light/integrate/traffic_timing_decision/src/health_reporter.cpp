#include "health_reporter.h"

#include <score/mw/health/common.h>

#include <chrono>
#include <string_view>
#include <utility>

#include "application_logger.h"

namespace {

enum class HealthIntervalMilliseconds : std::int64_t {
  kDecisionDeadlineMinimum = 0,
  kInternalProcessingCycle = 100,
  kSupervisorApiCycle = 500,
  kHeartbeatMinimum = 2000,
  kDecisionDeadlineMaximum = 2500,
  kHeartbeatMaximum = 3000,
};

constexpr std::chrono::milliseconds toDuration(
    const HealthIntervalMilliseconds value) noexcept {
  return std::chrono::milliseconds{static_cast<std::int64_t>(value)};
}

constexpr std::chrono::milliseconds kDecisionDeadlineMin =
    toDuration(HealthIntervalMilliseconds::kDecisionDeadlineMinimum);
constexpr std::chrono::milliseconds kDecisionDeadlineMax =
    toDuration(HealthIntervalMilliseconds::kDecisionDeadlineMaximum);
constexpr std::chrono::milliseconds kHeartbeatMin =
    toDuration(HealthIntervalMilliseconds::kHeartbeatMinimum);
constexpr std::chrono::milliseconds kHeartbeatMax =
    toDuration(HealthIntervalMilliseconds::kHeartbeatMaximum);
constexpr std::chrono::milliseconds kInternalProcessingCycle =
    toDuration(HealthIntervalMilliseconds::kInternalProcessingCycle);
constexpr std::chrono::milliseconds kSupervisorApiCycle =
    toDuration(HealthIntervalMilliseconds::kSupervisorApiCycle);
constexpr std::int32_t kHealthMonitorPriority = 60;

const score::mw::health::MonitorTag kDeadlineMonitorTag{
    "timing_decision_deadline_monitor"};
const score::mw::health::MonitorTag kHeartbeatMonitorTag{
    "timing_decision_heartbeat_monitor"};
const score::mw::health::DeadlineTag kDecisionCycleDeadlineTag{
    "decision_cycle_deadline"};

}  // namespace

HealthReporter::HealthReporter() = default;

HealthReporter::~HealthReporter() { shutdown(); }

bool HealthReporter::initialize() {
  if (initialized_) {
    return true;
  }

  using score::mw::health::HealthMonitorBuilder;
  using score::mw::health::SchedulerParameters;
  using score::mw::health::SchedulerPolicy;
  using score::mw::health::ThreadParameters;
  using score::mw::health::TimeRange;
  using score::mw::health::deadline::DeadlineMonitorBuilder;
  using score::mw::health::heartbeat::HeartbeatMonitorBuilder;

  score::mw::health::deadline::DeadlineMonitorBuilder deadlineBuilder =
      DeadlineMonitorBuilder().add_deadline(
      kDecisionCycleDeadlineTag,
      TimeRange{kDecisionDeadlineMin, kDecisionDeadlineMax});
  score::mw::health::heartbeat::HeartbeatMonitorBuilder heartbeatBuilder =
      HeartbeatMonitorBuilder(TimeRange{kHeartbeatMin, kHeartbeatMax});
  auto healthThreadParameters = ThreadParameters{}.scheduler_parameters(
      SchedulerParameters{SchedulerPolicy::RoundRobin,
                          kHealthMonitorPriority});

  score::cpp::expected<score::mw::health::HealthMonitor,
                       score::mw::health::Error>
      healthMonitorResult =
      HealthMonitorBuilder()
          .add_deadline_monitor(kDeadlineMonitorTag, std::move(deadlineBuilder))
          .add_heartbeat_monitor(kHeartbeatMonitorTag,
                                 std::move(heartbeatBuilder))
          .with_internal_processing_cycle(kInternalProcessingCycle)
          .with_supervisor_api_cycle(kSupervisorApiCycle)
          .thread_parameters(std::move(healthThreadParameters))
          .build();
  if (!healthMonitorResult.has_value()) {
    traffic_timing_decision::applicationLogger().LogError()
        << "[HEALTH] could not build official S-CORE HealthMonitor";
    return false;
  }
  healthMonitor_.emplace(std::move(healthMonitorResult.value()));

  score::cpp::expected<score::mw::health::deadline::DeadlineMonitor,
                       score::mw::health::Error>
      deadlineMonitorResult =
      healthMonitor_->get_deadline_monitor(kDeadlineMonitorTag);
  if (!deadlineMonitorResult.has_value()) {
    traffic_timing_decision::applicationLogger().LogError()
        << "[HEALTH] could not obtain deadline monitor";
    shutdown();
    return false;
  }
  deadlineMonitor_.emplace(std::move(deadlineMonitorResult.value()));

  score::cpp::expected<score::mw::health::heartbeat::HeartbeatMonitor,
                       score::mw::health::Error>
      heartbeatMonitorResult =
      healthMonitor_->get_heartbeat_monitor(kHeartbeatMonitorTag);
  if (!heartbeatMonitorResult.has_value()) {
    traffic_timing_decision::applicationLogger().LogError()
        << "[HEALTH] could not obtain heartbeat monitor";
    shutdown();
    return false;
  }
  heartbeatMonitor_.emplace(std::move(heartbeatMonitorResult.value()));

  score::cpp::expected<score::mw::health::deadline::Deadline,
                       score::mw::health::Error>
      deadlineResult =
      deadlineMonitor_->get_deadline(kDecisionCycleDeadlineTag);
  if (!deadlineResult.has_value()) {
    traffic_timing_decision::applicationLogger().LogError()
        << "[HEALTH] could not obtain decision cycle deadline";
    shutdown();
    return false;
  }
  cycleDeadline_.emplace(std::move(deadlineResult.value()));

  // S-CORE requires every configured local monitor to be obtained before the
  // worker starts. Starting here also guarantees Alive supervision is active
  // before run_application reports the process as Running after Initialize().
  healthMonitor_->start();
  monitoredCycleCount_ = std::uint64_t{};
  initialized_ = true;

  traffic_timing_decision::applicationLogger().LogInfo()
      << "[HEALTH][MONITOR] state=running; "
         "implementation=eclipse_score_health_monitor; evaluation_ms="
      << kInternalProcessingCycle.count()
      << "; heartbeat_ms=" << kHeartbeatMin.count() << ".."
      << kHeartbeatMax.count()
      << "; deadline_ms=" << kDecisionDeadlineMin.count() << ".."
      << kDecisionDeadlineMax.count()
      << "; worker_policy=SCHED_RR; worker_priority="
      << kHealthMonitorPriority;
  traffic_timing_decision::applicationLogger().LogInfo()
      << "[HEALTH][ALIVE] notifications=enabled; "
         "producer=health_monitor_worker; delivery=asynchronous; "
         "configured_min_interval_ms="
      << kSupervisorApiCycle.count() << "; receiver=launch_manager_phm";
  return true;
}

void HealthReporter::shutdown() {
  deadlineGuard_.reset();

  // Stop and join the HealthMonitor worker before releasing the state shared
  // by its deadline and heartbeat evaluation handles.
  healthMonitor_.reset();
  cycleDeadline_.reset();
  heartbeatMonitor_.reset();
  deadlineMonitor_.reset();

  if (initialized_) {
    traffic_timing_decision::applicationLogger().LogInfo()
        << "[HEALTH][STOP] monitor stopped";
  }
  monitoredCycleCount_ = std::uint64_t{};
  initialized_ = false;
}

bool HealthReporter::startDecisionCycle() {
  if (!initialized_ || !heartbeatMonitor_.has_value() ||
      !cycleDeadline_.has_value() || deadlineGuard_.has_value()) {
    traffic_timing_decision::applicationLogger().LogError()
        << "[HEALTH] invalid monitor state at cycle start";
    return false;
  }

  // One local heartbeat represents one released 2.5-second decision cycle.
  // It is input to HealthMonitor, not the Alive IPC notification itself. The
  // HealthMonitor worker emits Alive asynchronously when all local monitors
  // evaluate as healthy.
  ++monitoredCycleCount_;
  cycleStartedAt_ = std::chrono::steady_clock::now();
  heartbeatMonitor_->heartbeat();
#ifdef LOG_HEALTH_MONITOR
  traffic_timing_decision::applicationLogger().LogDebug()
      << "[HEALTH][HEARTBEAT] local_notification=recorded; cycle="
      << monitoredCycleCount_
      << "; expected_interval_ms=" << kHeartbeatMin.count() << ".."
      << kHeartbeatMax.count();
#endif

  // The deadline covers only the useful decision pipeline, not its periodic
  // wait between releases.
  score::cpp::expected<score::mw::health::deadline::DeadlineHandle,
                       score::mw::health::Error>
      deadlineResult = cycleDeadline_->start();
  if (!deadlineResult.has_value()) {
    traffic_timing_decision::applicationLogger().LogError()
        << "[HEALTH] could not start cycle deadline";
    return false;
  }

  deadlineGuard_.emplace(std::move(deadlineResult.value()));
  return true;
}

void HealthReporter::finishDecisionCycle() {
  if (!deadlineGuard_.has_value()) {
    traffic_timing_decision::applicationLogger().LogError()
        << "[HEALTH] no active deadline at cycle finish";
    return;
  }

  // Destroying the guard reports the end checkpoint to DeadlineMonitor.
  deadlineGuard_.reset();

#ifdef LOG_HEALTH_MONITOR
  const std::chrono::steady_clock::duration elapsed =
      std::chrono::steady_clock::now() - cycleStartedAt_;
  const std::int64_t elapsedMicroseconds =
      std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
  const bool withinConfiguredDeadline = elapsed <= kDecisionDeadlineMax;
  traffic_timing_decision::applicationLogger().LogDebug()
      << "[HEALTH][DEADLINE] local_window=closed; cycle="
      << monitoredCycleCount_ << "; elapsed_us=" << elapsedMicroseconds
      << "; max_ms=" << kDecisionDeadlineMax.count() << "; observed_status="
      << (withinConfiguredDeadline ? std::string_view{"met"}
                                   : std::string_view{"missed"});
#endif
}
