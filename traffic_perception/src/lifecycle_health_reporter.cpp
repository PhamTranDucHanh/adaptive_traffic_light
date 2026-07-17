#include "traffic_perception/lifecycle_health_reporter.h"

#include <chrono>
#include <iostream>
#include <utility>

#include <score/mw/health/common.h>

namespace {

using namespace std::chrono_literals;

constexpr auto kDeadlineMin = 0ms;
constexpr auto kDeadlineMax = 750ms;
constexpr auto kHeartbeatMin = 200ms;
constexpr auto kHeartbeatMax = 2200ms;
constexpr auto kInternalCycle = 50ms;
constexpr auto kAliveApiCycle = 500ms;

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
          kCycleDeadlineTag, TimeRange{kDeadlineMin, kDeadlineMax});
  auto heartbeatBuilder =
      HeartbeatMonitorBuilder(TimeRange{kHeartbeatMin, kHeartbeatMax});

  auto healthResult =
      HealthMonitorBuilder()
          .add_deadline_monitor(
              kDeadlineMonitorTag, std::move(deadlineBuilder))
          .add_heartbeat_monitor(
              kHeartbeatMonitorTag, std::move(heartbeatBuilder))
          .with_internal_processing_cycle(kInternalCycle)
          .with_supervisor_api_cycle(kAliveApiCycle)
          .build();
  if (!healthResult.has_value()) {
    std::cerr << "[PERCEPTION][HEALTH][ERROR] build failed\n";
    return false;
  }
  healthMonitor_.emplace(std::move(healthResult.value()));

  auto deadlineMonitorResult =
      healthMonitor_->get_deadline_monitor(kDeadlineMonitorTag);
  if (!deadlineMonitorResult.has_value()) {
    shutdown();
    return false;
  }
  deadlineMonitor_.emplace(std::move(deadlineMonitorResult.value()));

  auto heartbeatMonitorResult =
      healthMonitor_->get_heartbeat_monitor(kHeartbeatMonitorTag);
  if (!heartbeatMonitorResult.has_value()) {
    shutdown();
    return false;
  }
  heartbeatMonitor_.emplace(std::move(heartbeatMonitorResult.value()));

  auto deadlineResult =
      deadlineMonitor_->get_deadline(kCycleDeadlineTag);
  if (!deadlineResult.has_value()) {
    shutdown();
    return false;
  }
  cycleDeadline_.emplace(std::move(deadlineResult.value()));

  // Lấy tất cả configured local monitor trước khi start worker.
  healthMonitor_->start();
  initialized_ = true;
  std::cout << "[PERCEPTION][HEALTH][MONITOR] running; "
               "deadline_ms=0..750; heartbeat_ms=200..2200\n";
  return true;
}

bool LifecycleHealthReporter::beginCycle() {
  if (!initialized_ || !cycleDeadline_.has_value() ||
      activeDeadline_.has_value()) {
    return false;
  }

  auto result = cycleDeadline_->start();
  if (!result.has_value()) {
    return false;
  }
  activeDeadline_.emplace(std::move(result.value()));
  return true;
}

bool LifecycleHealthReporter::finishCycle(
    const bool snapshotPublished) {
  if (!activeDeadline_.has_value()) {
    return false;
  }

  // Destructor của DeadlineHandle đóng checkpoint cuối.
  activeDeadline_.reset();

  if (!snapshotPublished || !heartbeatMonitor_.has_value()) {
    return false;
  }

  // Outcome checkpoint: chỉ phát sau successful publication.
  heartbeatMonitor_->heartbeat();
  return true;
}

void LifecycleHealthReporter::shutdown() {
  activeDeadline_.reset();

  // Dừng/join HMON worker trước khi hủy các local monitor handle.
  healthMonitor_.reset();
  cycleDeadline_.reset();
  heartbeatMonitor_.reset();
  deadlineMonitor_.reset();
  initialized_ = false;
}

}  // namespace traffic_perception
