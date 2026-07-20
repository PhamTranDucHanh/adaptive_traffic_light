#include "common/health_reporter.h"

#include <chrono>
#include <iostream>
#include <utility>

#include <score/mw/health/common.h>

namespace {

using namespace std::chrono_literals;

constexpr auto kControlDeadlineMin = 0ms;
constexpr auto kControlDeadlineMax = 1000ms;
constexpr auto kHeartbeatMin = 500ms;
constexpr auto kHeartbeatMax = 1500ms;
constexpr auto kInternalProcessingCycle = 100ms;
constexpr auto kSupervisorApiCycle = 500ms;

const score::mw::health::MonitorTag kDeadlineMonitorTag{
    "signal_control_deadline_monitor"};
const score::mw::health::MonitorTag kHeartbeatMonitorTag{
    "signal_control_heartbeat_monitor"};
const score::mw::health::DeadlineTag kControlCycleDeadlineTag{
    "signal_control_cycle_deadline"};

}  // namespace

HealthReporter::HealthReporter() = default;

HealthReporter::~HealthReporter() { shutdown(); }

bool HealthReporter::initialize() {
  if (initialized_) {
    return true;
  }

  using score::mw::health::HealthMonitorBuilder;
  using score::mw::health::TimeRange;
  using score::mw::health::deadline::DeadlineMonitorBuilder;
  using score::mw::health::heartbeat::HeartbeatMonitorBuilder;

  auto deadlineBuilder = DeadlineMonitorBuilder().add_deadline(
      kControlCycleDeadlineTag,
      TimeRange{kControlDeadlineMin, kControlDeadlineMax});
  auto heartbeatBuilder =
      HeartbeatMonitorBuilder(TimeRange{kHeartbeatMin, kHeartbeatMax});

  auto healthMonitorResult =
      HealthMonitorBuilder()
          .add_deadline_monitor(kDeadlineMonitorTag,
                                std::move(deadlineBuilder))
          .add_heartbeat_monitor(kHeartbeatMonitorTag,
                                 std::move(heartbeatBuilder))
          .with_internal_processing_cycle(kInternalProcessingCycle)
          .with_supervisor_api_cycle(kSupervisorApiCycle)
          .build();
  if (!healthMonitorResult.has_value()) {
    std::cerr << "[TRAFFIC_SIGNAL_CONTROLLER][HEALTH][ERROR] could not build "
                 "official S-CORE HealthMonitor\n";
    return false;
  }
  healthMonitor_.emplace(std::move(healthMonitorResult.value()));

  auto deadlineMonitorResult =
      healthMonitor_->get_deadline_monitor(kDeadlineMonitorTag);
  if (!deadlineMonitorResult.has_value()) {
    std::cerr << "[TRAFFIC_SIGNAL_CONTROLLER][HEALTH][ERROR] could not obtain "
                 "deadline monitor\n";
    shutdown();
    return false;
  }
  deadlineMonitor_.emplace(std::move(deadlineMonitorResult.value()));

  auto heartbeatMonitorResult =
      healthMonitor_->get_heartbeat_monitor(kHeartbeatMonitorTag);
  if (!heartbeatMonitorResult.has_value()) {
    std::cerr << "[TRAFFIC_SIGNAL_CONTROLLER][HEALTH][ERROR] could not obtain "
                 "heartbeat monitor\n";
    shutdown();
    return false;
  }
  heartbeatMonitor_.emplace(std::move(heartbeatMonitorResult.value()));

  auto deadlineResult =
      deadlineMonitor_->get_deadline(kControlCycleDeadlineTag);
  if (!deadlineResult.has_value()) {
    std::cerr << "[TRAFFIC_SIGNAL_CONTROLLER][HEALTH][ERROR] could not obtain "
                 "control-cycle deadline\n";
    shutdown();
    return false;
  }
  cycleDeadline_.emplace(std::move(deadlineResult.value()));

  // Every configured local monitor must be obtained before the worker starts.
  // The worker emits Alive notifications only while local supervision passes.
  healthMonitor_->start();
  monitoredCycleCount_ = 0U;
  initialized_ = true;

  std::cout << "[TRAFFIC_SIGNAL_CONTROLLER][HEALTH][MONITOR] state=running; "
               "implementation=eclipse_score_health_monitor; evaluation_ms="
            << kInternalProcessingCycle.count() << "; heartbeat_ms="
            << kHeartbeatMin.count() << ".." << kHeartbeatMax.count()
            << "; deadline_ms=" << kControlDeadlineMin.count() << ".."
            << kControlDeadlineMax.count() << '\n';
  std::cout << "[TRAFFIC_SIGNAL_CONTROLLER][HEALTH][ALIVE] "
               "notifications=enabled; producer=health_monitor_worker; "
               "delivery=asynchronous; configured_min_interval_ms="
            << kSupervisorApiCycle.count()
            << "; receiver=launch_manager_phm\n";
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
    std::cout << "[TRAFFIC_SIGNAL_CONTROLLER][HEALTH][STOP] monitor stopped\n";
  }
  monitoredCycleCount_ = 0U;
  initialized_ = false;
}

bool HealthReporter::startControlCycle() {
  if (!initialized_ || !heartbeatMonitor_.has_value() ||
      !cycleDeadline_.has_value() || deadlineGuard_.has_value()) {
    std::cerr << "[TRAFFIC_SIGNAL_CONTROLLER][HEALTH][ERROR] invalid monitor "
                 "state at cycle start\n";
    return false;
  }

  // One heartbeat corresponds to one released 1-second control cycle. The
  // HealthMonitor worker converts healthy local supervision into Alive IPC.
  ++monitoredCycleCount_;
  cycleStartedAt_ = std::chrono::steady_clock::now();
  heartbeatMonitor_->heartbeat();
  std::cout << "[TRAFFIC_SIGNAL_CONTROLLER][HEALTH][HEARTBEAT] "
               "local_notification=recorded; cycle="
            << monitoredCycleCount_ << "; expected_interval_ms="
            << kHeartbeatMin.count() << ".." << kHeartbeatMax.count()
            << '\n';

  // The deadline covers only the useful control work, not the periodic wait.
  auto deadlineResult = cycleDeadline_->start();
  if (!deadlineResult.has_value()) {
    std::cerr << "[TRAFFIC_SIGNAL_CONTROLLER][HEALTH][ERROR] could not start "
                 "control-cycle deadline\n";
    return false;
  }

  deadlineGuard_.emplace(std::move(deadlineResult.value()));
  return true;
}

void HealthReporter::finishControlCycle() {
  if (!deadlineGuard_.has_value()) {
    std::cerr << "[TRAFFIC_SIGNAL_CONTROLLER][HEALTH][ERROR] no active "
                 "deadline at cycle finish\n";
    return;
  }

  // Destroying the guard reports the deadline's end checkpoint.
  deadlineGuard_.reset();

  const auto elapsed = std::chrono::steady_clock::now() - cycleStartedAt_;
  const auto elapsedMicroseconds =
      std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
  const bool withinConfiguredDeadline = elapsed <= kControlDeadlineMax;
  std::cout << "[TRAFFIC_SIGNAL_CONTROLLER][HEALTH][DEADLINE] "
               "local_window=closed; cycle="
            << monitoredCycleCount_ << "; elapsed_us=" << elapsedMicroseconds
            << "; max_ms=" << kControlDeadlineMax.count()
            << "; observed_status="
            << (withinConfiguredDeadline ? "met" : "missed") << '\n';
}

void HealthReporter::receiveHealthMetrics(const HealthStatus& metrics) {}

void HealthReporter::requestHeartbeat() {}

bool HealthReporter::checkHealth() { return true; }

HealthStatus HealthReporter::createHealthStatus() { return {}; }
