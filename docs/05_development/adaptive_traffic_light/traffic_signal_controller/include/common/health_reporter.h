#ifndef HEALTH_REPORTER_H
#define HEALTH_REPORTER_H

#include <chrono>
#include <cstdint>
#include <optional>

#include <score/mw/health/health_monitor.h>

// Thin application adapter around Eclipse S-CORE's official HealthMonitor.
// It owns the local deadline/heartbeat monitors and the background worker
// which sends Alive notifications to Launch Manager while they are healthy.
class HealthReporter {
 public:
  HealthReporter();
  ~HealthReporter();

  HealthReporter(const HealthReporter&) = delete;
  HealthReporter& operator=(const HealthReporter&) = delete;

  bool initialize();
  void shutdown();

  bool startControlCycle();
  void finishControlCycle();

 private:
  std::optional<score::mw::health::HealthMonitor> healthMonitor_;
  std::optional<score::mw::health::deadline::DeadlineMonitor>
      deadlineMonitor_;
  std::optional<score::mw::health::heartbeat::HeartbeatMonitor>
      heartbeatMonitor_;
  std::optional<score::mw::health::deadline::Deadline> cycleDeadline_;
  std::optional<score::mw::health::deadline::DeadlineHandle> deadlineGuard_;
  std::chrono::steady_clock::time_point cycleStartedAt_{};
  std::uint64_t monitoredCycleCount_{0U};
  bool initialized_{false};
};

#endif  // HEALTH_REPORTER_H
