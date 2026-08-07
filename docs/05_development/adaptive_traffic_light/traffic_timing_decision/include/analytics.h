#ifndef TRAFFIC_TIMING_DECISION_ANALYTICS_H
#define TRAFFIC_TIMING_DECISION_ANALYTICS_H

#include <cstdint>
#include <filesystem>
#include <iosfwd>

namespace traffic_timing_decision::analytics {

struct Statistics final {
  std::uint64_t sampleCount{};
  std::int64_t minimum{};
  double average{};
  double standardDeviation{};
  std::int64_t maximum{};
  std::int64_t p50{};
  std::int64_t p90{};
  std::int64_t p99{};
};

struct TimingAnalyticsReport final {
  Statistics wakeupLatencyUs{};
  Statistics executionTimeUs{};
  std::uint64_t deadlineMs{};
  std::uint64_t cycleDeadlineMisses{};
  std::uint64_t executionDeadlineMisses{};
};

// Parses preconverted S-CORE text captures, calculates nearest-rank
// percentiles, and prints the final report.
std::int32_t Run(const std::filesystem::path& wakeupDltPath,
                 const std::filesystem::path& executionDltPath,
                 std::ostream& output, std::ostream& errorOutput);

}  // namespace traffic_timing_decision::analytics

#endif  // TRAFFIC_TIMING_DECISION_ANALYTICS_H
