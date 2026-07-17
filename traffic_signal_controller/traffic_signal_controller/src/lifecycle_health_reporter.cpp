#include "traffic_signal_controller/lifecycle_health_reporter.h"

#include <chrono>
#include <iostream>
#include <utility>
#include <vector>

#include <score/mw/health/common.h>

namespace {

using namespace std::chrono_literals;
using score::mw::health::StateTag;

constexpr auto kDeadlineMin = 0ms;
constexpr auto kDeadlineMax = 80ms;
constexpr auto kHeartbeatMin = 10ms;
constexpr auto kHeartbeatMax = 300ms;
constexpr auto kInternalCycle = 10ms;
constexpr auto kAliveApiCycle = 100ms;

const score::mw::health::MonitorTag kDeadlineMonitorTag{
    "controller_deadline_monitor"};
const score::mw::health::MonitorTag kHeartbeatMonitorTag{
    "controller_heartbeat_monitor"};
const score::mw::health::MonitorTag kLogicMonitorTag{
    "controller_logic_monitor"};
const score::mw::health::DeadlineTag kTickDeadlineTag{
    "controller_tick_deadline"};

const StateTag kAllRedState{"all_red"};
const StateTag kNsGreenState{"ns_green"};
const StateTag kEwGreenState{"ew_green"};
const StateTag kYellowState{"yellow"};

const StateTag* stateTagFor(const PhaseId phase) noexcept {
  switch (phase) {
    case PhaseId::NS_GREEN:
      return &kNsGreenState;
    case PhaseId::EW_GREEN:
      return &kEwGreenState;
    case PhaseId::YELLOW:
      return &kYellowState;
    case PhaseId::ALL_RED:
      return &kAllRedState;
  }
  return nullptr;
}

}  // namespace

namespace traffic_signal_controller {

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
  using score::mw::health::logic::LogicMonitorBuilder;

  auto deadlineBuilder =
      DeadlineMonitorBuilder().add_deadline(
          kTickDeadlineTag, TimeRange{kDeadlineMin, kDeadlineMax});
  auto heartbeatBuilder =
      HeartbeatMonitorBuilder(TimeRange{kHeartbeatMin, kHeartbeatMax});

  auto logicBuilder =
      LogicMonitorBuilder{kAllRedState}
          .add_state(
              kAllRedState,
              std::vector<StateTag>{kNsGreenState, kEwGreenState})
          .add_state(
              kNsGreenState,
              std::vector<StateTag>{kYellowState})
          .add_state(
              kEwGreenState,
              std::vector<StateTag>{kYellowState})
          .add_state(
              kYellowState,
              std::vector<StateTag>{kAllRedState});

  auto healthResult =
      HealthMonitorBuilder()
          .add_deadline_monitor(
              kDeadlineMonitorTag, std::move(deadlineBuilder))
          .add_heartbeat_monitor(
              kHeartbeatMonitorTag, std::move(heartbeatBuilder))
          .add_logic_monitor(
              kLogicMonitorTag, std::move(logicBuilder))
          .with_internal_processing_cycle(kInternalCycle)
          .with_supervisor_api_cycle(kAliveApiCycle)
          .build();
  if (!healthResult.has_value()) {
    std::cerr << "[CONTROLLER][HEALTH][ERROR] build failed\n";
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

  auto logicMonitorResult =
      healthMonitor_->get_logic_monitor(kLogicMonitorTag);
  if (!logicMonitorResult.has_value()) {
    shutdown();
    return false;
  }
  logicMonitor_.emplace(std::move(logicMonitorResult.value()));

  auto deadlineResult =
      deadlineMonitor_->get_deadline(kTickDeadlineTag);
  if (!deadlineResult.has_value()) {
    shutdown();
    return false;
  }
  tickDeadline_.emplace(std::move(deadlineResult.value()));

  healthMonitor_->start();
  monitoredPhase_ = PhaseId::ALL_RED;
  initialized_ = true;
  std::cout << "[CONTROLLER][HEALTH][MONITOR] running; "
               "deadline_ms=0..80; heartbeat_ms=10..300; "
               "logic=enabled\n";
  return true;
}

bool LifecycleHealthReporter::beginTick() {
  if (!initialized_ || !tickDeadline_.has_value() ||
      activeDeadline_.has_value()) {
    return false;
  }

  auto result = tickDeadline_->start();
  if (!result.has_value()) {
    return false;
  }
  activeDeadline_.emplace(std::move(result.value()));
  return true;
}

bool LifecycleHealthReporter::finishTick(
    const PhaseId appliedPhase,
    const bool outputApplied) {
  if (!activeDeadline_.has_value()) {
    return false;
  }

  activeDeadline_.reset();

  if (!outputApplied || !heartbeatMonitor_.has_value()) {
    return false;
  }

  if (!reportAppliedPhase(appliedPhase)) {
    return false;
  }

  heartbeatMonitor_->heartbeat();
  return true;
}

bool LifecycleHealthReporter::reportAppliedPhase(
    const PhaseId appliedPhase) {
  if (!initialized_ || !logicMonitor_.has_value()) {
    return false;
  }

  // Không report self-transition; chỉ report state thực sự đã đổi.
  if (appliedPhase == monitoredPhase_) {
    return true;
  }

  const StateTag* const targetState = stateTagFor(appliedPhase);
  if (targetState == nullptr) {
    std::cerr
        << "[CONTROLLER][HEALTH][LOGIC][ERROR] unknown phase\n";
    return false;
  }

  auto result = logicMonitor_->transition(*targetState);
  if (!result.has_value()) {
    std::cerr
        << "[CONTROLLER][HEALTH][LOGIC][ERROR] invalid transition\n";
    return false;
  }

  monitoredPhase_ = appliedPhase;
  return true;
}

void LifecycleHealthReporter::shutdown() {
  activeDeadline_.reset();
  healthMonitor_.reset();
  tickDeadline_.reset();
  logicMonitor_.reset();
  heartbeatMonitor_.reset();
  deadlineMonitor_.reset();
  monitoredPhase_ = PhaseId::ALL_RED;
  initialized_ = false;
}

}  // namespace traffic_signal_controller