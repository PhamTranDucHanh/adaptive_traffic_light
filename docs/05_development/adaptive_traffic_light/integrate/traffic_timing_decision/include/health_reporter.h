#ifndef HEALTH_REPORTER_H
#define HEALTH_REPORTER_H

#include <score/mw/health/health_monitor.h>

#include <chrono>
#include <cstdint>
#include <optional>

// #define LOG_HEALTH_MONITOR

// Thin application adapter around Eclipse S-CORE's official HealthMonitor.
// It owns both local monitors and the background worker which reports the
// process checkpoint to Launch Manager through the Alive API.

class HealthReporter {
 public:
  HealthReporter();
  ~HealthReporter();

  HealthReporter(const HealthReporter&) = delete;
  HealthReporter& operator=(const HealthReporter&) = delete;

  bool initialize();
  void shutdown();

  bool startDecisionCycle();
  void finishDecisionCycle();

 private:
  std::optional<score::mw::health::HealthMonitor> healthMonitor_;
  std::optional<score::mw::health::deadline::DeadlineMonitor> deadlineMonitor_;
  std::optional<score::mw::health::heartbeat::HeartbeatMonitor>
      heartbeatMonitor_;
  std::optional<score::mw::health::deadline::Deadline> cycleDeadline_;
  std::optional<score::mw::health::deadline::DeadlineHandle> deadlineGuard_;
  std::chrono::steady_clock::time_point cycleStartedAt_{};
  std::chrono::steady_clock::time_point nextHeartbeatAt_{};
  std::uint64_t monitoredCycleCount_{};
  bool initialized_{false};
};

#endif  // !HEALTH_REPORTER_H
