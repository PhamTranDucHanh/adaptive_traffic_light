#ifndef TRAFFIC_TIMING_DECISION_TIMING_REPORT_LOGGER_H
#define TRAFFIC_TIMING_DECISION_TIMING_REPORT_LOGGER_H

#include <cstdint>
#include <memory>
#include <string_view>

namespace score::mw::log {
class Recorder;
}

namespace traffic_timing_decision {

struct WakeupTimingRecord final {
  std::uint64_t cycleId{};
  std::uint64_t scheduledReleaseNs{};
  std::uint64_t actualWakeupNs{};
  std::uint64_t wakeupLatencyNs{};
  std::uint64_t periodNs{};
};

struct ExecutionTimingRecord final {
  std::uint64_t cycleId{};
  std::uint64_t executionStartNs{};
  std::uint64_t executionEndNs{};
  std::uint64_t executionTimeNs{};
  std::uint64_t responseTimeNs{};
  std::uint64_t deadlineNs{};
  std::uint64_t executionOverrunNs{};
  std::uint64_t cycleOverrunNs{};
  bool executionDeadlineMiss{};
  bool cycleDeadlineMiss{};
  bool cycleSucceeded{};
};

// Owns two independent S-CORE DLT file recorders. A normal CreateLogger()
// cannot be used here because every Logger in one process shares the same
// process-wide recorder and therefore the same <appId>.dlt output file.
class TimingReportLogger final {
 public:
  TimingReportLogger();
  ~TimingReportLogger();

  TimingReportLogger(const TimingReportLogger&) = delete;
  TimingReportLogger& operator=(const TimingReportLogger&) = delete;

  bool initialize(std::string_view outputDirectory);
  void shutdown() noexcept;
  bool logWakeup(const WakeupTimingRecord& record) noexcept;
  bool logExecution(const ExecutionTimingRecord& record) noexcept;

 private:
  std::unique_ptr<score::mw::log::Recorder> wakeupRecorder_;
  std::unique_ptr<score::mw::log::Recorder> executionRecorder_;
};

}  // namespace traffic_timing_decision

#endif  // TRAFFIC_TIMING_DECISION_TIMING_REPORT_LOGGER_H
