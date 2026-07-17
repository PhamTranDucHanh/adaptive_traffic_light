#ifndef TRAFFIC_SIGNAL_CONTROLLER_LIFECYCLE_HEALTH_REPORTER_H_
#define TRAFFIC_SIGNAL_CONTROLLER_LIFECYCLE_HEALTH_REPORTER_H_

#include <optional>

#include <score/mw/health/health_monitor.h>

#include "common/config.h"

namespace traffic_signal_controller {

class LifecycleHealthReporter final {
 public:
  LifecycleHealthReporter() = default;
  ~LifecycleHealthReporter();

  LifecycleHealthReporter(const LifecycleHealthReporter&) = delete;
  LifecycleHealthReporter& operator=(const LifecycleHealthReporter&) = delete;

  bool initialize();
  bool beginTick();
  bool finishTick(PhaseId appliedPhase, bool outputApplied);
  bool reportAppliedPhase(PhaseId appliedPhase);
  void shutdown();

 private:
  std::optional<score::mw::health::HealthMonitor> healthMonitor_;
  std::optional<score::mw::health::deadline::DeadlineMonitor>
      deadlineMonitor_;
  std::optional<score::mw::health::heartbeat::HeartbeatMonitor>
      heartbeatMonitor_;
  std::optional<score::mw::health::logic::LogicMonitor> logicMonitor_;
  std::optional<score::mw::health::deadline::Deadline> tickDeadline_;
  std::optional<score::mw::health::deadline::DeadlineHandle>
      activeDeadline_;
  PhaseId monitoredPhase_{PhaseId::ALL_RED};
  bool initialized_{false};
};

}  // namespace traffic_signal_controller

#endif  // TRAFFIC_SIGNAL_CONTROLLER_LIFECYCLE_HEALTH_REPORTER_H_