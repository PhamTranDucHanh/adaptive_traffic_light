#include "traffic_perception/lifecycle_health_reporter.h"

#include <chrono>
#include <iostream>
#include <utility>

#include <score/mw/health/common.h>

namespace {

using namespace std::chrono_literals;

constexpr auto kPerceptionDeadlineMin = 0ms;
constexpr auto kPerceptionDeadlineMax = 20000ms;
constexpr auto kHeartbeatMin = 2500ms;
constexpr auto kHeartbeatMax = 25000ms;
constexpr auto kInternalProcessingCycle = 100ms;
constexpr auto kSupervisorApiCycle = 500ms;

const score::mw::health::MonitorTag kDeadlineMonitorTag{
    "perception_deadline_monitor"};
const score::mw::health::MonitorTag kHeartbeatMonitorTag{
    "perception_heartbeat_monitor"};
const score::mw::health::DeadlineTag kCycleDeadlineTag{
    "perception_cycle_deadline"};

}  // namespace

namespace traffic_perception {

LifecycleHealthReporter::~LifecycleHealthReporter() {
  shutdown();
}

bool LifecycleHealthReporter::initialize() {
  if (initialized_) {
    return true;
  }

  using score::mw::health::HealthMonitorBuilder;
  using score::mw::health::TimeRange;
  using score::mw::health::deadline::DeadlineMonitorBuilder;
  using score::mw::health::heartbeat::HeartbeatMonitorBuilder;

  auto deadlineBuilder =
      DeadlineMonitorBuilder().add_deadline(
          kCycleDeadlineTag,
          TimeRange{kPerceptionDeadlineMin, kPerceptionDeadlineMax});
  auto heartbeatBuilder =
      HeartbeatMonitorBuilder(TimeRange{kHeartbeatMin, kHeartbeatMax});

  auto healthResult =
      HealthMonitorBuilder()
          .add_deadline_monitor(
              kDeadlineMonitorTag, std::move(deadlineBuilder))
          .add_heartbeat_monitor(
              kHeartbeatMonitorTag, std::move(heartbeatBuilder))
          .with_internal_processing_cycle(kInternalProcessingCycle)
          .with_supervisor_api_cycle(kSupervisorApiCycle)
          .build();
  if (!healthResult.has_value()) {
    std::cerr << "[TRAFFIC_PERCEPTION][HEALTH][ERROR] could not build "
                 "official S-CORE HealthMonitor\n";
    return false;
  }
  healthMonitor_.emplace(std::move(healthResult.value()));

  auto deadlineMonitorResult =
      healthMonitor_->get_deadline_monitor(kDeadlineMonitorTag);
  if (!deadlineMonitorResult.has_value()) {
    std::cerr << "[TRAFFIC_PERCEPTION][HEALTH][ERROR] could not obtain "
                 "deadline monitor\n";
    shutdown();
    return false;
  }
  deadlineMonitor_.emplace(std::move(deadlineMonitorResult.value()));

  auto heartbeatMonitorResult =
      healthMonitor_->get_heartbeat_monitor(kHeartbeatMonitorTag);
  if (!heartbeatMonitorResult.has_value()) {
    std::cerr << "[TRAFFIC_PERCEPTION][HEALTH][ERROR] could not obtain "
                 "heartbeat monitor\n";
    shutdown();
    return false;
  }
  heartbeatMonitor_.emplace(std::move(heartbeatMonitorResult.value()));

  auto deadlineResult =
      deadlineMonitor_->get_deadline(kCycleDeadlineTag);
  if (!deadlineResult.has_value()) {
    std::cerr << "[TRAFFIC_PERCEPTION][HEALTH][ERROR] could not obtain "
                 "perception-cycle deadline\n";
    shutdown();
    return false;
  }
  cycleDeadline_.emplace(std::move(deadlineResult.value()));

  // Obtain every configured local monitor before starting the worker. The
  // worker emits Alive notifications only while local supervision passes.
  healthMonitor_->start();
  monitoredCycleCount_ = 0U;
  initialized_ = true;
  std::cout << "[TRAFFIC_PERCEPTION][HEALTH][MONITOR] state=running; "
               "implementation=eclipse_score_health_monitor; evaluation_ms="
            << kInternalProcessingCycle.count() << "; heartbeat_ms="
            << kHeartbeatMin.count() << ".." << kHeartbeatMax.count()
            << "; deadline_ms=" << kPerceptionDeadlineMin.count() << ".."
            << kPerceptionDeadlineMax.count() << '\n';
  std::cout << "[TRAFFIC_PERCEPTION][HEALTH][ALIVE] "
               "notifications=enabled; producer=health_monitor_worker; "
               "delivery=asynchronous; configured_min_interval_ms="
            << kSupervisorApiCycle.count()
            << "; receiver=launch_manager_phm\n";
  return true;
}

bool LifecycleHealthReporter::startPerceptionCycle() {
  if (!initialized_ || !heartbeatMonitor_.has_value() ||
      !cycleDeadline_.has_value() ||
      activeDeadline_.has_value()) {
    std::cerr << "[TRAFFIC_PERCEPTION][HEALTH][ERROR] invalid monitor state "
                 "at cycle start\n";
    return false;
  }

  ++monitoredCycleCount_;
  cycleStartedAt_ = std::chrono::steady_clock::now();
  heartbeatMonitor_->heartbeat();
  std::cout << "[TRAFFIC_PERCEPTION][HEALTH][HEARTBEAT] "
               "local_notification=recorded; cycle="
            << monitoredCycleCount_ << "; expected_interval_ms="
            << kHeartbeatMin.count() << ".." << kHeartbeatMax.count()
            << '\n';

  // The deadline covers useful perception work, not the periodic wait.
  auto result = cycleDeadline_->start();
  if (!result.has_value()) {
    std::cerr << "[TRAFFIC_PERCEPTION][HEALTH][ERROR] could not start "
                 "perception-cycle deadline\n";
    return false;
  }
  activeDeadline_.emplace(std::move(result.value()));
  return true;
}

void LifecycleHealthReporter::finishPerceptionCycle() {
  if (!activeDeadline_.has_value()) {
    std::cerr << "[TRAFFIC_PERCEPTION][HEALTH][ERROR] no active deadline at "
                 "cycle finish\n";
    return;
  }

  // Destroying DeadlineHandle records the deadline's end checkpoint.
  activeDeadline_.reset();

  const auto elapsed = std::chrono::steady_clock::now() - cycleStartedAt_;
  const auto elapsedMicroseconds =
      std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
  const bool withinConfiguredDeadline =
      elapsed <= kPerceptionDeadlineMax;
  std::cout << "[TRAFFIC_PERCEPTION][HEALTH][DEADLINE] "
               "local_window=closed; cycle="
            << monitoredCycleCount_ << "; elapsed_us=" << elapsedMicroseconds
            << "; max_ms=" << kPerceptionDeadlineMax.count()
            << "; observed_status="
            << (withinConfiguredDeadline ? "met" : "missed") << '\n';
}

void LifecycleHealthReporter::shutdown() {
  activeDeadline_.reset();

  // Stop and join the HealthMonitor worker before releasing local handles.
  healthMonitor_.reset();
  cycleDeadline_.reset();
  heartbeatMonitor_.reset();
  deadlineMonitor_.reset();
  if (initialized_) {
    std::cout << "[TRAFFIC_PERCEPTION][HEALTH][STOP] monitor stopped\n";
  }
  monitoredCycleCount_ = 0U;
  initialized_ = false;
}

}  // namespace traffic_perception
