#ifndef TRAFFIC_PERCEPTION_LIFECYCLE_HEALTH_REPORTER_H_
#define TRAFFIC_PERCEPTION_LIFECYCLE_HEALTH_REPORTER_H_

#include <chrono>
#include <cstdint>
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
  void shutdown();
  bool reportHeartbeat();
  bool startPerceptionCycle();
  void finishPerceptionCycle();

 private:
  std::optional<score::mw::health::HealthMonitor> healthMonitor_;
  std::optional<score::mw::health::deadline::DeadlineMonitor>
      deadlineMonitor_;
  std::optional<score::mw::health::heartbeat::HeartbeatMonitor>
      heartbeatMonitor_;
  std::optional<score::mw::health::deadline::Deadline> cycleDeadline_;
  std::optional<score::mw::health::deadline::DeadlineHandle>
      activeDeadline_;
  std::chrono::steady_clock::time_point cycleStartedAt_{};
  std::uint64_t monitoredCycleCount_{0U};
  bool initialized_{false};
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_LIFECYCLE_HEALTH_REPORTER_H_
