#include "analytics.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

namespace {

constexpr std::string_view kWakeupTextFileName{"wakeup_latency.txt"};
constexpr std::string_view kExecutionTextFileName{"execution_time.txt"};
constexpr std::string_view kWakeupLatencyField{"wakeup_latency_us="};
constexpr std::string_view kExecutionTimeField{"execution_time_us="};
constexpr std::string_view kDeadlineField{"deadline_ms="};
constexpr std::string_view kExecutionDeadlineMissField{
    "execution_deadline_miss="};
constexpr std::string_view kCycleDeadlineMissField{"cycle_deadline_miss="};
constexpr std::string_view kTrueValue{"true"};
constexpr std::string_view kFalseValue{"false"};
constexpr std::string_view kWorkspaceEnvironment{"BUILD_WORKSPACE_DIRECTORY"};
constexpr std::string_view kDefaultOutputDirectory{
    "traffic_timing_decision/output"};
constexpr std::string_view kNestedOutputDirectory{
    "traffic_timing_decision/traffic_timing_decision/output"};
constexpr std::size_t kStartupCycleCount{15U};
enum class ExitCode : std::int32_t {
  kSuccess = 0,
  kFailure = 1,
  kInvalidArguments = 2
};

enum class CommandLineArgument : std::int32_t {
  kProgramOnlyCount = 1,
  kCustomPathsCount = 3,
  kWakeupPathIndex = 1,
  kExecutionPathIndex = 2,
};

enum class Percentile : std::uint64_t {
  kP50 = 50U,
  kP90 = 90U,
  kP99 = 99U,
};

enum class PercentageScale : std::uint64_t {
  kFull = 100U,
};

enum class OutputFormat : std::int32_t {
  kAveragePrecision = 2,
};

constexpr std::int32_t toValue(const ExitCode value) noexcept {
  return static_cast<std::int32_t>(value);
}

constexpr std::int32_t toValue(const CommandLineArgument value) noexcept {
  return static_cast<std::int32_t>(value);
}

constexpr std::uint64_t toValue(const Percentile value) noexcept {
  return static_cast<std::uint64_t>(value);
}

constexpr std::uint64_t toValue(const PercentageScale value) noexcept {
  return static_cast<std::uint64_t>(value);
}

constexpr std::int32_t toValue(const OutputFormat value) noexcept {
  return static_cast<std::int32_t>(value);
}

template <typename Integer>
bool extractInteger(const std::string_view line, const std::string_view field,
                    Integer& value) noexcept {
  const std::string_view::size_type fieldPosition = line.find(field);
  if (fieldPosition == std::string_view::npos) {
    return false;
  }

  std::string_view::size_type valuePosition = fieldPosition + field.size();
  while (valuePosition < line.size() && line[valuePosition] == ' ') {
    ++valuePosition;
  }

  const char* const begin = line.data() + valuePosition;
  const char* const end = line.data() + line.size();
  const std::from_chars_result parseResult = std::from_chars(begin, end, value);
  return parseResult.ec == std::errc{} && parseResult.ptr != begin;
}

bool extractBoolean(const std::string_view line, const std::string_view field,
                    bool& value) noexcept {
  const std::string_view::size_type fieldPosition = line.find(field);
  if (fieldPosition == std::string_view::npos) {
    return false;
  }

  std::string_view::size_type valuePosition = fieldPosition + field.size();
  while (valuePosition < line.size() && line[valuePosition] == ' ') {
    ++valuePosition;
  }

  const std::string_view remaining = line.substr(valuePosition);
  if (remaining.substr(std::string_view::size_type{}, kTrueValue.size()) ==
      kTrueValue) {
    value = true;
    return true;
  }
  if (remaining.substr(std::string_view::size_type{}, kFalseValue.size()) ==
      kFalseValue) {
    value = false;
    return true;
  }
  if (!remaining.empty() && remaining.front() == '0') {
    value = false;
    return true;
  }
  if (!remaining.empty() && remaining.front() == '1') {
    value = true;
    return true;
  }
  return false;
}

template <typename Integer>
Integer nearestRankPercentile(const std::vector<Integer>& sortedValues,
                              const Percentile percentile) noexcept {
  const std::uint64_t sampleCount =
      static_cast<std::uint64_t>(sortedValues.size());
  const std::uint64_t percentage = toValue(percentile);
  const std::uint64_t percentageScale = toValue(PercentageScale::kFull);

  // ceil(sampleCount * percentage / 100), calculated without overflowing the
  // multiplication for large sample sets.
  const std::uint64_t quotient = sampleCount / percentageScale;
  const std::uint64_t remainder = sampleCount % percentageScale;
  const std::uint64_t rank =
      quotient * percentage +
      ((remainder * percentage) + percentageScale - std::uint64_t{1U}) /
          percentageScale;
  const std::uint64_t zeroBasedIndex = rank - std::uint64_t{1U};
  return sortedValues[static_cast<typename std::vector<Integer>::size_type>(
      zeroBasedIndex)];
}

template <typename Integer, typename Statistics>
bool calculateStatistics(std::vector<Integer> values, Statistics& statistics,
                         std::string& error) {
  if (values.empty()) {
    error = "no timing samples were found";
    return false;
  }

  std::sort(values.begin(), values.end());
  std::int64_t total{};
  for (const Integer value : values) {
    total += static_cast<std::int64_t>(value);
  }

  statistics.sampleCount = static_cast<std::uint64_t>(values.size());
  statistics.minimum = values.front();
  statistics.average =
      static_cast<double>(total) / static_cast<double>(statistics.sampleCount);

  long double squaredDeviationTotal{};
  const long double average = static_cast<long double>(statistics.average);
  for (const Integer value : values) {
    const long double deviation = static_cast<long double>(value) - average;
    squaredDeviationTotal += deviation * deviation;
  }
  statistics.standardDeviation = static_cast<double>(
      std::sqrt(squaredDeviationTotal /
                static_cast<long double>(statistics.sampleCount)));

  statistics.maximum = values.back();
  statistics.p50 = nearestRankPercentile(values, Percentile::kP50);
  statistics.p90 = nearestRankPercentile(values, Percentile::kP90);
  statistics.p99 = nearestRankPercentile(values, Percentile::kP99);
  return true;
}

template <typename Value>
bool excludeStartupCycles(std::vector<Value>& values, std::string& error) {
  if (values.size() <= kStartupCycleCount) {
    error = "not enough timing samples after excluding the first " +
            std::to_string(kStartupCycleCount) + " startup cycles";
    return false;
  }

  values.erase(values.begin(),
               values.begin() +
                   static_cast<std::ptrdiff_t>(kStartupCycleCount));
  return true;
}

bool parseWakeupText(const std::filesystem::path& textPath,
                     std::vector<std::int64_t>& wakeupLatencies,
                     std::string& error) {
  std::ifstream input{textPath};
  if (!input.is_open()) {
    error = "could not open decoded wake-up file: " + textPath.string();
    return false;
  }

  std::uint64_t lineNumber{};
  std::string line{};
  while (std::getline(input, line)) {
    ++lineNumber;
    if (line.find(kWakeupLatencyField) == std::string::npos) {
      continue;
    }

    std::int64_t wakeupLatency{};
    if (!extractInteger(line, kWakeupLatencyField, wakeupLatency)) {
      error = "invalid wakeup_latency_us at line " + std::to_string(lineNumber);
      return false;
    }
    wakeupLatencies.push_back(wakeupLatency);
  }

  if (input.bad()) {
    error = "failed while reading decoded wake-up file: " + textPath.string();
    return false;
  }
  if (wakeupLatencies.empty()) {
    error = "decoded wake-up file contains no wakeup_latency_us records";
    return false;
  }
  return true;
}

bool parseExecutionText(
    const std::filesystem::path& textPath,
    std::vector<std::uint64_t>& executionTimes,
    std::vector<bool>& executionDeadlineMisses,
    std::vector<bool>& cycleDeadlineMisses,
    traffic_timing_decision::analytics::TimingAnalyticsReport& report,
    std::string& error) {
  std::ifstream input{textPath};
  if (!input.is_open()) {
    error = "could not open decoded execution file: " + textPath.string();
    return false;
  }

  bool deadlineInitialized{false};
  std::uint64_t lineNumber{};
  std::string line{};
  while (std::getline(input, line)) {
    ++lineNumber;
    if (line.find(kExecutionTimeField) == std::string::npos) {
      continue;
    }

    std::uint64_t executionTime{};
    std::uint64_t deadline{};
    bool executionDeadlineMiss{};
    bool cycleDeadlineMiss{};
    if (!extractInteger(line, kExecutionTimeField, executionTime) ||
        !extractInteger(line, kDeadlineField, deadline) ||
        !extractBoolean(line, kExecutionDeadlineMissField,
                        executionDeadlineMiss) ||
        !extractBoolean(line, kCycleDeadlineMissField, cycleDeadlineMiss)) {
      error = "invalid execution timing record at line " +
              std::to_string(lineNumber);
      return false;
    }

    if (!deadlineInitialized) {
      report.deadlineMs = deadline;
      deadlineInitialized = true;
    } else if (deadline != report.deadlineMs) {
      error = "inconsistent deadline_ms at line " + std::to_string(lineNumber);
      return false;
    }

    executionTimes.push_back(executionTime);
    executionDeadlineMisses.push_back(executionDeadlineMiss);
    cycleDeadlineMisses.push_back(cycleDeadlineMiss);
  }

  if (input.bad()) {
    error = "failed while reading decoded execution file: " + textPath.string();
    return false;
  }
  if (executionTimes.empty()) {
    error = "decoded execution file contains no execution_time_us records";
    return false;
  }
  return true;
}

template <typename Statistics>
void printStatistics(const Statistics& statistics, std::ostream& output) {
  const auto printTimingValue = [&output](const auto value) {
    constexpr long double kMicrosecondsPerMillisecond{1000.0L};
    const long double valueInMicroseconds = static_cast<long double>(value);

    if (valueInMicroseconds >= kMicrosecondsPerMillisecond) {
      output << std::fixed
             << std::setprecision(toValue(OutputFormat::kAveragePrecision))
             << valueInMicroseconds / kMicrosecondsPerMillisecond << " (ms)";
      return;
    }

    if constexpr (std::is_floating_point_v<std::remove_cv_t<decltype(value)>>) {
      output << std::fixed
             << std::setprecision(toValue(OutputFormat::kAveragePrecision));
    }
    output << value << " (us)";
  };

  output << "Statistics\n"
         << "----------\n"
         << "Samples : " << statistics.sampleCount << '\n'
         << "Min     : ";
  printTimingValue(statistics.minimum);
  output << "\nAvg     : ";
  printTimingValue(statistics.average);
  output << "\nStd Dev : ";
  printTimingValue(statistics.standardDeviation);
  output << "\nMax     : ";
  printTimingValue(statistics.maximum);
  output << "\nP50     : ";
  printTimingValue(statistics.p50);
  output << "\nP90     : ";
  printTimingValue(statistics.p90);
  output << "\nP99     : ";
  printTimingValue(statistics.p99);
  output << '\n';
}

void printReport(
    const traffic_timing_decision::analytics::TimingAnalyticsReport& report,
    std::ostream& output) {
  output << "Analysis window: excluded first " << kStartupCycleCount
         << " cycles (startup guard band); no shutdown cycles excluded\n"
         << "Deadline Misses (" << report.deadlineMs
         << " ms): cycle=" << report.cycleDeadlineMisses << " / "
         << report.executionTimeUs.sampleCount
         << ", execution=" << report.executionDeadlineMisses << " / "
         << report.executionTimeUs.sampleCount << " cycles\n\n"
         << "========================================\n"
         << "WAKE-UP LATENCY\n"
         << "========================================\n";
  printStatistics(report.wakeupLatencyUs, output);
  output << '\n'
         << "========================================\n"
         << "DECISION EXECUTION TIME\n"
         << "========================================\n";
  printStatistics(report.executionTimeUs, output);
}

std::filesystem::path defaultOutputDirectory() {
  const char* const workspaceDirectory =
      std::getenv(kWorkspaceEnvironment.data());
  if (workspaceDirectory != nullptr && workspaceDirectory[0] != '\0') {
    return std::filesystem::path{workspaceDirectory} / kDefaultOutputDirectory;
  }

  const std::filesystem::path workingDirectory =
      std::filesystem::current_path();
  const std::filesystem::path directCandidate =
      workingDirectory / kDefaultOutputDirectory;
  if (std::filesystem::exists(directCandidate / kWakeupTextFileName) &&
      std::filesystem::exists(directCandidate / kExecutionTextFileName)) {
    return directCandidate;
  }
  return workingDirectory / kNestedOutputDirectory;
}

}  // namespace

namespace traffic_timing_decision::analytics {

std::int32_t Run(const std::filesystem::path& wakeupDltPath,
                 const std::filesystem::path& executionDltPath,
                 std::ostream& output, std::ostream& errorOutput) {
  const std::filesystem::path wakeupTextPath{wakeupDltPath};
  const std::filesystem::path executionTextPath{executionDltPath};

  std::string error{};
  if (!std::filesystem::exists(wakeupTextPath)) {
    errorOutput << "[ANALYTICS][ERROR] wake-up text file does not exist: "
                << wakeupTextPath << '\n';
    return toValue(ExitCode::kFailure);
  }
  if (!std::filesystem::exists(executionTextPath)) {
    errorOutput << "[ANALYTICS][ERROR] execution text file does not exist: "
                << executionTextPath << '\n';
    return toValue(ExitCode::kFailure);
  }

  TimingAnalyticsReport report{};
  std::vector<std::int64_t> wakeupLatencies{};
  std::vector<std::uint64_t> executionTimes{};
  std::vector<bool> executionDeadlineMisses{};
  std::vector<bool> cycleDeadlineMisses{};
  if (!parseWakeupText(wakeupTextPath, wakeupLatencies, error) ||
      !parseExecutionText(executionTextPath, executionTimes,
                          executionDeadlineMisses, cycleDeadlineMisses, report,
                          error) ||
      !excludeStartupCycles(wakeupLatencies, error) ||
      !excludeStartupCycles(executionTimes, error) ||
      !excludeStartupCycles(executionDeadlineMisses, error) ||
      !excludeStartupCycles(cycleDeadlineMisses, error) ||
      !calculateStatistics(wakeupLatencies, report.wakeupLatencyUs, error) ||
      !calculateStatistics(executionTimes, report.executionTimeUs, error)) {
    errorOutput << "[ANALYTICS][ERROR] " << error << '\n';
    return toValue(ExitCode::kFailure);
  }

  report.executionDeadlineMisses = static_cast<std::uint64_t>(std::count(
      executionDeadlineMisses.begin(), executionDeadlineMisses.end(), true));
  report.cycleDeadlineMisses = static_cast<std::uint64_t>(
      std::count(cycleDeadlineMisses.begin(), cycleDeadlineMisses.end(), true));

  printReport(report, output);
  output << "\nDecoded text files:\n"
         << "  " << wakeupTextPath << '\n'
         << "  " << executionTextPath << '\n';
  return toValue(ExitCode::kSuccess);
}

}  // namespace traffic_timing_decision::analytics

// Entry point for the analytics component. This is a standalone program that
// can be run after the traffic application has been executed to completion. It
// reads the decoded wake-up and execution timing text files and calculates the
// necessary statistics.
std::int32_t main(const std::int32_t argumentCount,
                  const char* const arguments[]) {
  std::filesystem::path wakeupDltPath{};
  std::filesystem::path executionDltPath{};
  if (argumentCount == toValue(CommandLineArgument::kProgramOnlyCount)) {
    const std::filesystem::path outputDirectory = defaultOutputDirectory();
    wakeupDltPath = outputDirectory / kWakeupTextFileName;
    executionDltPath = outputDirectory / kExecutionTextFileName;
  } else if (argumentCount == toValue(CommandLineArgument::kCustomPathsCount)) {
    wakeupDltPath = arguments[toValue(CommandLineArgument::kWakeupPathIndex)];
    executionDltPath =
        arguments[toValue(CommandLineArgument::kExecutionPathIndex)];
  } else {
    std::cerr << "Usage: analytics [wakeup_latency.txt execution_time.txt]\n";
    return toValue(ExitCode::kInvalidArguments);
  }

  return traffic_timing_decision::analytics::Run(
      wakeupDltPath, executionDltPath, std::cout, std::cerr);
}
