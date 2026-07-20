#ifndef COMMON_PERIODIC_HEALTH_REPORTER_H
#define COMMON_PERIODIC_HEALTH_REPORTER_H

#include <chrono>
#include <cstdint>
#include <optional>

#include <score/mw/health/health_monitor.h>

namespace common {

enum class HealthProfile {
  kPerception,
  kSignalController,
};

class PeriodicHealthReporter {
 public:
  explicit PeriodicHealthReporter(HealthProfile profile) noexcept;
  ~PeriodicHealthReporter();

  PeriodicHealthReporter(const PeriodicHealthReporter&) = delete;
  PeriodicHealthReporter& operator=(const PeriodicHealthReporter&) = delete;

  bool initialize();
  void shutdown();
  bool startCycle();
  bool finishCycle();

  std::uint64_t cycleCount() const noexcept;
  std::int64_t lastCycleElapsedUs() const noexcept;

 private:
  bool initializeWithTags(const score::mw::health::MonitorTag& deadlineTag,
                          const score::mw::health::MonitorTag& heartbeatTag,
                          const score::mw::health::DeadlineTag& cycleTag);

  HealthProfile profile_;
  std::optional<score::mw::health::HealthMonitor> healthMonitor_;
  std::optional<score::mw::health::deadline::DeadlineMonitor>
      deadlineMonitor_;
  std::optional<score::mw::health::heartbeat::HeartbeatMonitor>
      heartbeatMonitor_;
  std::optional<score::mw::health::deadline::Deadline> cycleDeadline_;
  std::optional<score::mw::health::deadline::DeadlineHandle> deadlineGuard_;
  std::chrono::steady_clock::time_point cycleStartedAt_{};
  std::uint64_t cycleCount_{0U};
  std::int64_t lastCycleElapsedUs_{0};
  bool initialized_{false};
};

}  // namespace common

#endif  // COMMON_PERIODIC_HEALTH_REPORTER_H
