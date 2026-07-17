#ifndef TRAFFIC_PERCEPTION_LIFECYCLE_HEALTH_REPORTER_H_
#define TRAFFIC_PERCEPTION_LIFECYCLE_HEALTH_REPORTER_H_

#include <optional>

#include <score/mw/health/health_monitor.h>

namespace traffic_perception {

class LifecycleHealthReporter final {
 public:
  LifecycleHealthReporter() = default;
  ~LifecycleHealthReporter();

  LifecycleHealthReporter(const LifecycleHealthReporter&) = delete;
  LifecycleHealthReporter& operator=(const LifecycleHealthReporter&) = delete;

  bool initialize();
  bool beginCycle();
  bool finishCycle(bool snapshotPublished);
  void shutdown();

 private:
  std::optional<score::mw::health::HealthMonitor> healthMonitor_;
  std::optional<score::mw::health::deadline::DeadlineMonitor>
      deadlineMonitor_;
  std::optional<score::mw::health::heartbeat::HeartbeatMonitor>
      heartbeatMonitor_;
  std::optional<score::mw::health::deadline::Deadline> cycleDeadline_;
  std::optional<score::mw::health::deadline::DeadlineHandle>
      activeDeadline_;
  bool initialized_{false};
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_LIFECYCLE_HEALTH_REPORTER_H_
