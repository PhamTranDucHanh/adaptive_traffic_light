#include "common/health_reporter.h"

#include <chrono>
#include <utility>

#include "common/config.h"
#include "common/logging_contexts.h"
#include <score/mw/health/common.h>
#include "score/mw/log/logger.h"

namespace {

const score::mw::health::MonitorTag kDeadlineMonitorTag{
    "signal_control_deadline_monitor"};
const score::mw::health::MonitorTag kHeartbeatMonitorTag{
    "signal_control_heartbeat_monitor"};
const score::mw::health::DeadlineTag kControlCycleDeadlineTag{
    "signal_control_cycle_deadline"};

score::mw::log::Logger& Logger() {
  static auto& logger =
      score::mw::log::CreateLogger(ctrl::logging::kCtxHealth,
                                   "Health Reporter");
  return logger;
}

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

  auto deadlineBuilder = DeadlineMonitorBuilder().add_deadline(
      kControlCycleDeadlineTag,
      TimeRange{kControlDeadlineMin, kControlDeadlineMax});
  auto heartbeatBuilder =
      HeartbeatMonitorBuilder(TimeRange{kHeartbeatMin, kHeartbeatMax});
  auto healthThreadParameters = ThreadParameters{}.scheduler_parameters(
      SchedulerParameters{SchedulerPolicy::RoundRobin,
                          kHealthMonitorPriority});

  auto healthMonitorResult =
      HealthMonitorBuilder()
          .add_deadline_monitor(kDeadlineMonitorTag,
                                std::move(deadlineBuilder))
          .add_heartbeat_monitor(kHeartbeatMonitorTag,
                                 std::move(heartbeatBuilder))
          .with_internal_processing_cycle(kInternalProcessingCycle)
          .with_supervisor_api_cycle(kSupervisorApiCycle)
          .thread_parameters(std::move(healthThreadParameters))
          .build();
  if (!healthMonitorResult.has_value()) {
    Logger().LogWarn() << "event=HEALTH_MONITOR_BUILD_FAILED"
                       << ", implementation=eclipse_score_health_monitor";
    return false;
  }
  healthMonitor_.emplace(std::move(healthMonitorResult.value()));

  auto deadlineMonitorResult =
      healthMonitor_->get_deadline_monitor(kDeadlineMonitorTag);
  if (!deadlineMonitorResult.has_value()) {
    Logger().LogWarn() << "event=DEADLINE_MONITOR_UNAVAILABLE"
                       << ", monitor=signal_control_deadline_monitor";
    shutdown();
    return false;
  }
  deadlineMonitor_.emplace(std::move(deadlineMonitorResult.value()));

  auto heartbeatMonitorResult =
      healthMonitor_->get_heartbeat_monitor(kHeartbeatMonitorTag);
  if (!heartbeatMonitorResult.has_value()) {
    Logger().LogWarn() << "event=HEARTBEAT_MONITOR_UNAVAILABLE"
                       << ", monitor=signal_control_heartbeat_monitor";
    shutdown();
    return false;
  }
  heartbeatMonitor_.emplace(std::move(heartbeatMonitorResult.value()));

  auto deadlineResult =
      deadlineMonitor_->get_deadline(kControlCycleDeadlineTag);
  if (!deadlineResult.has_value()) {
    Logger().LogWarn() << "event=CONTROL_DEADLINE_UNAVAILABLE"
                       << ", deadline=signal_control_cycle_deadline";
    shutdown();
    return false;
  }
  cycleDeadline_.emplace(std::move(deadlineResult.value()));

  // Every configured local monitor must be obtained before the worker starts.
  // The worker emits Alive notifications only while local supervision passes.
  healthMonitor_->start();
  monitoredCycleCount_ = 0U;
  initialized_ = true;

  Logger().LogInfo() << "event=HEALTH_MONITOR_STARTED"
                     << ", implementation=eclipse_score_health_monitor"
                     << ", evaluation_ms=" << kInternalProcessingCycle.count()
                     << ", worker_policy=SCHED_RR"
                     << ", worker_priority=" << kHealthMonitorPriority
                     << ", heartbeat_min_ms=" << kHeartbeatMin.count()
                     << ", heartbeat_max_ms=" << kHeartbeatMax.count()
                     << ", deadline_min_ms=" << kControlDeadlineMin.count()
                     << ", deadline_max_ms=" << kControlDeadlineMax.count();
  Logger().LogInfo() << "event=ALIVE_NOTIFICATIONS_ENABLED"
                     << ", producer=health_monitor_worker"
                     << ", delivery=asynchronous"
                     << ", supervisor_interval_ms="
                     << kSupervisorApiCycle.count()
                     << ", receiver=launch_manager_phm";
  return true;
}

void HealthReporter::shutdown() {
  deadlineGuard_.reset();

  // Destroying HealthMonitor stops and joins its background worker before the
  // local monitor handles are released.
  healthMonitor_.reset();
  cycleDeadline_.reset();
  heartbeatMonitor_.reset();
  deadlineMonitor_.reset();

  if (initialized_) {
    Logger().LogInfo() << "event=HEALTH_MONITOR_STOPPED";
  }
  monitoredCycleCount_ = 0U;
  initialized_ = false;
}

bool HealthReporter::startControlCycle() {
  if (!initialized_ || !heartbeatMonitor_.has_value() ||
      !cycleDeadline_.has_value() || deadlineGuard_.has_value()) {
    Logger().LogWarn() << "event=CONTROL_CYCLE_START_FAILED"
                       << ", reason=INVALID_MONITOR_STATE";
    return false;
  }

  // Deadline-monitor every control cycle; heartbeat-monitor every configured
  // sampling interval. The worker converts healthy supervision into Alive IPC.
  ++monitoredCycleCount_;
  cycleStartedAt_ = std::chrono::steady_clock::now();
  const bool heartbeatDue =
      ((monitoredCycleCount_ - 1U) % kHeartbeatControlCycleInterval) == 0U;
  if (heartbeatDue) {
    heartbeatMonitor_->heartbeat();
    Logger().LogInfo() << "event=HEARTBEAT_RECORDED"
                       << ", cycle=" << monitoredCycleCount_
                       << ", nominal_interval_ms="
                       << (kHeartbeatControlCycleInterval * 1000U)
                       << ", expected_min_ms=" << kHeartbeatMin.count()
                       << ", expected_max_ms=" << kHeartbeatMax.count();
  }

  // The deadline covers only the useful control work, not the periodic wait.
  auto deadlineResult = cycleDeadline_->start();
  if (!deadlineResult.has_value()) {
    Logger().LogWarn() << "event=CONTROL_DEADLINE_START_FAILED";
    return false;
  }

  deadlineGuard_.emplace(std::move(deadlineResult.value()));
  return true;
}

void HealthReporter::finishControlCycle() {
  if (!deadlineGuard_.has_value()) {
    Logger().LogWarn() << "event=CONTROL_DEADLINE_FINISH_FAILED"
                       << ", reason=NO_ACTIVE_DEADLINE";
    return;
  }

  // Destroying the guard reports the deadline's end checkpoint.
  deadlineGuard_.reset();

  const auto elapsed = std::chrono::steady_clock::now() - cycleStartedAt_;
  const auto elapsedMicroseconds =
      std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
  const bool withinConfiguredDeadline = elapsed <= kControlDeadlineMax;
  Logger().LogInfo() << "event=CONTROL_DEADLINE_CLOSED"
                     << ", cycle=" << monitoredCycleCount_
                     << ", elapsed_us=" << elapsedMicroseconds
                     << ", max_ms=" << kControlDeadlineMax.count()
                     << ", observed_status="
                     << (withinConfiguredDeadline ? "met" : "missed");
}
