#include "common/periodic_health_reporter.h"

#include <utility>

#include <score/mw/health/common.h>

namespace {

using namespace std::chrono_literals;

constexpr auto kDeadlineMin = 0ms;
constexpr auto kDeadlineMax = 3000ms;
constexpr auto kHeartbeatMin = 2500ms;
constexpr auto kHeartbeatMax = 3500ms;
constexpr auto kInternalProcessingCycle = 100ms;
constexpr auto kSupervisorApiCycle = 500ms;

const score::mw::health::MonitorTag kPerceptionDeadlineTag{
    "perception_deadline_monitor"};
const score::mw::health::MonitorTag kPerceptionHeartbeatTag{
    "perception_heartbeat_monitor"};
const score::mw::health::DeadlineTag kPerceptionCycleTag{
    "perception_cycle_deadline"};

const score::mw::health::MonitorTag kSignalDeadlineTag{
    "signal_control_deadline_monitor"};
const score::mw::health::MonitorTag kSignalHeartbeatTag{
    "signal_control_heartbeat_monitor"};
const score::mw::health::DeadlineTag kSignalCycleTag{
    "signal_control_cycle_deadline"};

}  // namespace

namespace common {

PeriodicHealthReporter::PeriodicHealthReporter(
    const HealthProfile profile) noexcept
    : profile_{profile} {}

PeriodicHealthReporter::~PeriodicHealthReporter() { shutdown(); }

bool PeriodicHealthReporter::initialize() {
  if (initialized_) {
    return true;
  }

  if (profile_ == HealthProfile::kPerception) {
    return initializeWithTags(kPerceptionDeadlineTag,
                              kPerceptionHeartbeatTag,
                              kPerceptionCycleTag);
  }
  return initializeWithTags(kSignalDeadlineTag, kSignalHeartbeatTag,
                            kSignalCycleTag);
}

bool PeriodicHealthReporter::initializeWithTags(
    const score::mw::health::MonitorTag& deadlineTag,
    const score::mw::health::MonitorTag& heartbeatTag,
    const score::mw::health::DeadlineTag& cycleTag) {
  using score::mw::health::HealthMonitorBuilder;
  using score::mw::health::TimeRange;
  using score::mw::health::deadline::DeadlineMonitorBuilder;
  using score::mw::health::heartbeat::HeartbeatMonitorBuilder;

  auto deadlineBuilder = DeadlineMonitorBuilder().add_deadline(
      cycleTag, TimeRange{kDeadlineMin, kDeadlineMax});
  auto heartbeatBuilder =
      HeartbeatMonitorBuilder(TimeRange{kHeartbeatMin, kHeartbeatMax});

  auto healthResult =
      HealthMonitorBuilder()
          .add_deadline_monitor(deadlineTag, std::move(deadlineBuilder))
          .add_heartbeat_monitor(heartbeatTag, std::move(heartbeatBuilder))
          .with_internal_processing_cycle(kInternalProcessingCycle)
          .with_supervisor_api_cycle(kSupervisorApiCycle)
          .build();
  if (!healthResult.has_value()) {
    return false;
  }
  healthMonitor_.emplace(std::move(healthResult.value()));

  auto deadlineMonitorResult = healthMonitor_->get_deadline_monitor(deadlineTag);
  if (!deadlineMonitorResult.has_value()) {
    shutdown();
    return false;
  }
  deadlineMonitor_.emplace(std::move(deadlineMonitorResult.value()));

  auto heartbeatMonitorResult =
      healthMonitor_->get_heartbeat_monitor(heartbeatTag);
  if (!heartbeatMonitorResult.has_value()) {
    shutdown();
    return false;
  }
  heartbeatMonitor_.emplace(std::move(heartbeatMonitorResult.value()));

  auto deadlineResult = deadlineMonitor_->get_deadline(cycleTag);
  if (!deadlineResult.has_value()) {
    shutdown();
    return false;
  }
  cycleDeadline_.emplace(std::move(deadlineResult.value()));

  healthMonitor_->start();
  cycleCount_ = 0U;
  lastCycleElapsedUs_ = 0;
  initialized_ = true;
  return true;
}

void PeriodicHealthReporter::shutdown() {
  deadlineGuard_.reset();
  healthMonitor_.reset();
  cycleDeadline_.reset();
  heartbeatMonitor_.reset();
  deadlineMonitor_.reset();
  initialized_ = false;
}

bool PeriodicHealthReporter::startCycle() {
  if (!initialized_ || !heartbeatMonitor_.has_value() ||
      !cycleDeadline_.has_value() || deadlineGuard_.has_value()) {
    return false;
  }

  ++cycleCount_;
  cycleStartedAt_ = std::chrono::steady_clock::now();
  heartbeatMonitor_->heartbeat();
  auto deadlineResult = cycleDeadline_->start();
  if (!deadlineResult.has_value()) {
    return false;
  }
  deadlineGuard_.emplace(std::move(deadlineResult.value()));
  return true;
}

bool PeriodicHealthReporter::finishCycle() {
  if (!deadlineGuard_.has_value()) {
    return false;
  }
  deadlineGuard_.reset();
  lastCycleElapsedUs_ =
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - cycleStartedAt_)
          .count();
  return true;
}

std::uint64_t PeriodicHealthReporter::cycleCount() const noexcept {
  return cycleCount_;
}

std::int64_t PeriodicHealthReporter::lastCycleElapsedUs() const noexcept {
  return lastCycleElapsedUs_;
}

}  // namespace common
