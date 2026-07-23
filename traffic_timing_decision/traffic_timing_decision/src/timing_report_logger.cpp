#include "timing_report_logger.h"

#include <memory>
#include <string_view>

#include "score/memory.hpp"
#include "score/mw/log/configuration/configuration.h"
#include "score/mw/log/detail/file_recorder/file_recorder_factory.h"
#include "score/mw/log/log_level.h"
#include "score/mw/log/recorder.h"

namespace {

constexpr std::string_view kEcuId{"TLGT"};
constexpr std::string_view kWakeupApplicationId{"WKUP"};
constexpr std::string_view kWakeupContextId{"LATN"};
constexpr std::string_view kExecutionApplicationId{"EXEC"};
constexpr std::string_view kExecutionContextId{"TIME"};

enum class TimeUnitScale : std::uint64_t {
  kNanosecondsPerMicrosecond = 1000ULL,
  kNanosecondsPerMillisecond = 1000000ULL,
};

constexpr std::uint64_t scaleValue(const TimeUnitScale value) noexcept {
  return static_cast<std::uint64_t>(value);
}

constexpr std::uint64_t nanosecondsToMicroseconds(
    const std::uint64_t nanoseconds) noexcept {
  return nanoseconds / scaleValue(TimeUnitScale::kNanosecondsPerMicrosecond);
}

constexpr std::int64_t nanosecondsToMicroseconds(
    const std::int64_t nanoseconds) noexcept {
  return nanoseconds / static_cast<std::int64_t>(scaleValue(
                           TimeUnitScale::kNanosecondsPerMicrosecond));
}

constexpr std::uint64_t nanosecondsToMilliseconds(
    const std::uint64_t nanoseconds) noexcept {
  return nanoseconds / scaleValue(TimeUnitScale::kNanosecondsPerMillisecond);
}

std::unique_ptr<score::mw::log::Recorder> createFileRecorder(
    const std::string_view applicationId,
    const std::string_view outputDirectory) {
  score::mw::log::detail::Configuration configuration{};
  configuration.SetEcuId(kEcuId);
  configuration.SetAppId(applicationId);
  configuration.SetLogFilePath(outputDirectory);
  configuration.SetDefaultLogLevel(score::mw::log::LogLevel::kDebug);

  auto* const memoryResource = score::cpp::pmr::get_default_resource();
  score::mw::log::detail::FileRecorderFactory factory{
      score::os::Fcntl::Default(memoryResource)};
  return factory.CreateLogRecorder(configuration, memoryResource);
}

}  // namespace

namespace traffic_timing_decision {

TimingReportLogger::TimingReportLogger() = default;

TimingReportLogger::~TimingReportLogger() = default;

bool TimingReportLogger::initialize(const std::string_view outputDirectory) {
  shutdown();
  wakeupRecorder_ = createFileRecorder(kWakeupApplicationId, outputDirectory);
  executionRecorder_ =
      createFileRecorder(kExecutionApplicationId, outputDirectory);
  if (wakeupRecorder_ == nullptr || executionRecorder_ == nullptr) {
    shutdown();
    return false;
  }
  return true;
}

void TimingReportLogger::shutdown() noexcept {
  executionRecorder_.reset();
  wakeupRecorder_.reset();
}

bool TimingReportLogger::logWakeup(const WakeupTimingRecord& record) noexcept {
  if (wakeupRecorder_ == nullptr) {
    return false;
  }

  auto slot = wakeupRecorder_->StartRecord(kWakeupContextId,
                                           score::mw::log::LogLevel::kInfo);
  if (!slot.has_value()) {
    return false;
  }

  const auto& handle = slot.value();
  wakeupRecorder_->Log(handle, std::string_view{"cycle_id="});
  wakeupRecorder_->Log(handle, record.cycleId);
  wakeupRecorder_->Log(handle, std::string_view{"; scheduled_release_ns="});
  wakeupRecorder_->Log(handle, record.scheduledReleaseNs);
  wakeupRecorder_->Log(handle, std::string_view{"; actual_wakeup_ns="});
  wakeupRecorder_->Log(handle, record.actualWakeupNs);
  wakeupRecorder_->Log(handle, std::string_view{"; wakeup_latency_us="});
  wakeupRecorder_->Log(handle,
                       nanosecondsToMicroseconds(record.wakeupLatencyNs));
  wakeupRecorder_->Log(handle, std::string_view{"; period_ms="});
  wakeupRecorder_->Log(handle, nanosecondsToMilliseconds(record.periodNs));
  wakeupRecorder_->StopRecord(handle);
  return true;
}

bool TimingReportLogger::logExecution(
    const ExecutionTimingRecord& record) noexcept {
  if (executionRecorder_ == nullptr) {
    return false;
  }

  const score::mw::log::LogLevel level = record.cycleDeadlineMiss
                                             ? score::mw::log::LogLevel::kWarn
                                             : score::mw::log::LogLevel::kInfo;
  auto slot = executionRecorder_->StartRecord(kExecutionContextId, level);
  if (!slot.has_value()) {
    return false;
  }

  const auto& handle = slot.value();
  executionRecorder_->Log(handle, std::string_view{"cycle_id="});
  executionRecorder_->Log(handle, record.cycleId);
  executionRecorder_->Log(handle, std::string_view{"; execution_start_ns="});
  executionRecorder_->Log(handle, record.executionStartNs);
  executionRecorder_->Log(handle, std::string_view{"; execution_end_ns="});
  executionRecorder_->Log(handle, record.executionEndNs);
  executionRecorder_->Log(handle, std::string_view{"; execution_time_us="});
  executionRecorder_->Log(handle,
                          nanosecondsToMicroseconds(record.executionTimeNs));
  executionRecorder_->Log(handle, std::string_view{"; response_time_ns="});
  executionRecorder_->Log(handle, record.responseTimeNs);
  executionRecorder_->Log(handle, std::string_view{"; deadline_ms="});
  executionRecorder_->Log(handle, nanosecondsToMilliseconds(record.deadlineNs));
  executionRecorder_->Log(handle,
                          std::string_view{"; execution_deadline_miss="});
  executionRecorder_->Log(handle, record.executionDeadlineMiss);
  executionRecorder_->Log(handle, std::string_view{"; execution_overrun_ns="});
  executionRecorder_->Log(handle, record.executionOverrunNs);
  executionRecorder_->Log(handle, std::string_view{"; cycle_deadline_miss="});
  executionRecorder_->Log(handle, record.cycleDeadlineMiss);
  executionRecorder_->Log(handle, std::string_view{"; cycle_overrun_ns="});
  executionRecorder_->Log(handle, record.cycleOverrunNs);
  executionRecorder_->Log(handle, std::string_view{"; cycle_succeeded="});
  executionRecorder_->Log(handle, record.cycleSucceeded);
  executionRecorder_->StopRecord(handle);
  return true;
}

}  // namespace traffic_timing_decision
