#include "analytics_service/analytics.h"

#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/config.h"

namespace {

struct TimeDisplayUnit {
  const char* name;
  double nanosecondsPerUnit;
};

TimeDisplayUnit SelectTimeDisplayUnit(const double referenceNanoseconds) {
  if (referenceNanoseconds < 1'000.0) {
    return {"ns", 1.0};
  }
  if (referenceNanoseconds < 1'000'000.0) {
    return {"us", 1'000.0};
  }
  if (referenceNanoseconds < 1'000'000'000.0) {
    return {"ms", 1'000'000.0};
  }
  if (referenceNanoseconds < 60'000'000'000.0) {
    return {"s", 1'000'000'000.0};
  }
  if (referenceNanoseconds < 3'600'000'000'000.0) {
    return {"min", 60'000'000'000.0};
  }
  return {"h", 3'600'000'000'000.0};
}

double ConvertNanoseconds(const long double nanoseconds,
                          const TimeDisplayUnit& unit) {
  return static_cast<double>(nanoseconds / unit.nanosecondsPerUnit);
}

std::string FormatNanoseconds(const long double nanoseconds) {
  const auto unit = SelectTimeDisplayUnit(static_cast<double>(nanoseconds));
  std::ostringstream output;
  output << std::fixed << std::setprecision(3)
         << ConvertNanoseconds(nanoseconds, unit) << ' ' << unit.name;
  return output.str();
}

std::string Trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");

  if (first == std::string::npos) {
    return {};
  }

  const auto last = value.find_last_not_of(" \t\r\n");

  return value.substr(first, last - first + 1U);
}

bool ParseUint64(const std::string& value, std::uint64_t& result) noexcept {
  if (value.empty() ||
      value.find_first_not_of("0123456789") != std::string::npos) {
    return false;
  }

  try {
    std::size_t processedCharacters{};
    const auto parsedValue = std::stoull(value, &processedCharacters);

    if (processedCharacters != value.size()) {
      return false;
    }

    result = parsedValue;
    return true;
  } catch (...) {
    return false;
  }
}

bool FileExistsAndIsNotEmpty(const std::string& path) {
  struct stat fileStatus {};

  return ::stat(path.c_str(), &fileStatus) == 0 && fileStatus.st_size > 0;
}

bool EnsureDirectoryExists(const std::string& path) {
  if (path.empty()) {
    return false;
  }

  std::string currentPath;

  for (const char character : path) {
    currentPath.push_back(character);

    if (character != '/') {
      continue;
    }

    if (currentPath.size() == 1U) {
      continue;
    }

    if (::mkdir(currentPath.c_str(), 0755) != 0 && errno != EEXIST) {
      std::cerr << "Unable to create output directory: " << currentPath
                << ", reason=" << std::strerror(errno) << '\n';
      return false;
    }
  }

  if (::mkdir(path.c_str(), 0755) != 0 && errno != EEXIST) {
    std::cerr << "Unable to create output directory: " << path
              << ", reason=" << std::strerror(errno) << '\n';
    return false;
  }

  return true;
}

bool CopyFile(const std::string& sourcePath,
              const std::string& destinationPath) {
  std::ifstream source(sourcePath, std::ios::binary);

  if (!source.is_open()) {
    std::cerr << "Unable to open archive source file: " << sourcePath << '\n';
    return false;
  }

  std::ofstream destination(destinationPath,
                            std::ios::binary | std::ios::trunc);

  if (!destination.is_open()) {
    std::cerr << "Unable to open archive destination file: "
              << destinationPath << '\n';
    return false;
  }

  destination << source.rdbuf();
  return destination.good();
}

std::uint32_t NextArchiveNumber(const std::string& outputDirectory) {
  std::uint32_t nextArchiveNumber{1U};
  DIR* directory = ::opendir(outputDirectory.c_str());

  if (directory == nullptr) {
    return nextArchiveNumber;
  }

  while (const dirent* entry = ::readdir(directory)) {
    unsigned int archiveNumber{};

    if (std::strstr(entry->d_name, "_analytics_input.txt") != nullptr &&
        std::sscanf(entry->d_name, "%3u_", &archiveNumber) == 1 &&
        archiveNumber >= nextArchiveNumber) {
      nextArchiveNumber = archiveNumber + 1U;
    }
  }

  ::closedir(directory);
  return nextArchiveNumber;
}

}  // namespace

Analytics::Analytics(std::string logFilePath)
    : logFilePath_(std::move(logFilePath)) {}

bool Analytics::Analyze() {
  logEntries_.clear();
  displayStatistics_ = {};
  planStatistics_ = {};
  emergencyStatistics_ = {};
  wakeupStatistics_ = {};

  if (!LoadLog()) {
    return false;
  }     

  ComputeDisplayStatistics();
  ComputePlanStatistics();
  ComputeEmergencyStatistics();
  ComputeWakeupStatistics();

  return true;
}

bool Analytics::LoadLog() {
  std::ifstream input(logFilePath_, std::ios::binary);

  if (!input.is_open()) {
    std::cerr << "Unable to open log file: " << logFilePath_ << '\n';

    return false;
  }

  const std::string content{std::istreambuf_iterator<char>{input},
                            std::istreambuf_iterator<char>{}};

  if (LooksLikeDltBinary(content)) {
    return LoadDltBinaryLog(content);
  }

  std::istringstream textInput(content);
  std::string line;

  while (std::getline(textInput, line)) {
    if (Trim(line).empty()) {
      continue;
    }

    LogEntry entry;

    if (ParseLogLine(line, entry)) {
      ParseApplicationMessage(entry);
      logEntries_.push_back(std::move(entry));
    }
  }

  return !logEntries_.empty();
}

bool Analytics::LoadDltBinaryLog(const std::string& content) {
  std::ofstream convertedOutput(logFilePath_ + ".txt");
  std::size_t searchPosition{};
  std::uint64_t recordIndex{};

  while (true) {
    const auto recordStart = content.find("DLT", searchPosition);

    if (recordStart == std::string::npos) {
      break;
    }

    auto recordEnd = content.find("DLT", recordStart + 3U);

    if (recordEnd == std::string::npos) {
      recordEnd = content.size();
    }

    const std::string record =
        content.substr(recordStart, recordEnd - recordStart);

    std::string contextId;
    const std::string message =
        ConvertDltRecordToTextMessage(record, contextId);

    if (!message.empty()) {
      LogEntry entry;
      entry.timestampNs = ExtractDltStorageTimestampNs(record);

      if (entry.timestampNs == 0U) {
        entry.timestampNs = recordIndex * 1'000'000'000ULL;
      }

      entry.message = message;

      ParseApplicationMessage(entry);
      logEntries_.push_back(std::move(entry));

      if (convertedOutput.is_open()) {
        convertedOutput << "CTRL " << contextId << ' ' << message << '\n';
      }
    }

    ++recordIndex;
    searchPosition = recordEnd;
  }

  return !logEntries_.empty();
}

bool Analytics::ParseLogLine(const std::string& line, LogEntry& entry) const {
  std::istringstream stream(line);

  std::string date;
  std::string time;
  std::string dltTimestamp;
  std::string messageCounter;
  std::string messageType;
  std::string ecuId;
  std::string applicationId;
  std::string contextId;
  std::string logLevel;
  std::string verbose;
  std::string argumentCount;

  if (!(stream >> date >> time >> dltTimestamp >> messageCounter >>
        messageType >> ecuId >> applicationId >> contextId >>
        messageType >> logLevel >> verbose >> argumentCount)) {
    return false;
  }

  double dltTimestampSeconds{};

  try {
    dltTimestampSeconds = std::stod(dltTimestamp);
  } catch (...) {
    dltTimestampSeconds = 0.0;
  }

  entry.timestampNs =
      static_cast<std::uint64_t>(dltTimestampSeconds * 1'000'000'000.0);

  std::getline(stream, entry.message);
  entry.message = Trim(entry.message);

  return !entry.message.empty();
}

void Analytics::ParseApplicationMessage(LogEntry& entry) const {
  const std::string eventName = ExtractValue(entry.message, "event");

  entry.eventType = EventTypeFromString(eventName);

  entry.planId = ParseUint64OrZero(ExtractValue(entry.message, "plan_id"));

  entry.emergencyNs = ParseBool(ExtractValue(entry.message, "emergency_ns"));

  entry.emergencyEw = ParseBool(ExtractValue(entry.message, "emergency_ew"));

  entry.rejectReason = ExtractValue(entry.message, "reason");
}

Analytics::EventType Analytics::EventTypeFromString(
    const std::string& eventName) {
  static const std::unordered_map<std::string, EventType> eventMap{
      {"PLAN_RECEIVED", EventType::PlanReceived},
      {"PLAN_VALIDATED", EventType::PlanValidated},
      {"PLAN_REJECTED", EventType::PlanRejected},
      {"PLAN_QUEUED", EventType::PlanQueued},
      {"PLAN_PUBLISHED", EventType::PlanPublished},
      {"PLAN_CONSUMED", EventType::PlanConsumed},
      {"PLAN_APPLIED", EventType::PlanApplied},

      {"EMERGENCY_QUEUED", EventType::EmergencyQueued},
      {"EMERGENCY_CONSUMED", EventType::EmergencyConsumed},
      {"EMERGENCY_ACCEPTED", EventType::EmergencyAccepted},
      {"EMERGENCY_REJECTED", EventType::EmergencyRejected},
      {"EMERGENCY_APPLIED", EventType::EmergencyApplied},

      {"TIMING_DECISION_RECEIVE_TO_CONTROLLER_RECEIVE",
       EventType::DecisionToControllerLatency},
      {"PERCEPTION_PUBLISH_TO_CONTROLLER_RECEIVE",
       EventType::PerceptionToControllerLatency},
      {"EMERGENCY_RECEIVE_TO_APPLY", EventType::EmergencyReceiveToApply},

      {"PHASE_ENTER", EventType::PhaseEnter}};

  const auto iterator = eventMap.find(eventName);

  if (iterator == eventMap.end()) {
    return EventType::Unknown;
  }

  return iterator->second;
}

std::string Analytics::ExtractValue(const std::string& message,
                                    const std::string& key) {
  const std::string token = key + "=";

  const auto tokenPosition = message.find(token);

  if (tokenPosition == std::string::npos) {
    return {};
  }

  const auto valueStart = tokenPosition + token.size();

  const auto valueEnd = message.find_first_of(",;", valueStart);

  if (valueEnd == std::string::npos) {
    return Trim(message.substr(valueStart));
  }

  return Trim(message.substr(valueStart, valueEnd - valueStart));
}

std::uint64_t Analytics::ParseUint64OrZero(const std::string& value) {
  std::uint64_t result{};
  return ParseUint64(value, result) ? result : 0U;
}

bool Analytics::ParseBool(const std::string& value) {
  return value == "True" || value == "true" || value == "1";
}

std::uint64_t Analytics::ExtractDltStorageTimestampNs(
    const std::string& record) {
  if (record.size() < 12U || record.compare(0U, 3U, "DLT") != 0) {
    return 0U;
  }

  const auto readLittleEndianUint32 =
      [&record](const std::size_t offset) -> std::uint32_t {
    return static_cast<std::uint32_t>(
        static_cast<unsigned char>(record[offset]) |
        (static_cast<std::uint32_t>(
             static_cast<unsigned char>(record[offset + 1U]))
         << 8U) |
        (static_cast<std::uint32_t>(
             static_cast<unsigned char>(record[offset + 2U]))
         << 16U) |
        (static_cast<std::uint32_t>(
             static_cast<unsigned char>(record[offset + 3U]))
         << 24U));
  };

  const std::uint64_t seconds = readLittleEndianUint32(4U);
  const std::uint64_t microseconds = readLittleEndianUint32(8U);

  return (seconds * kNanosecondsPerSecond) +
         (microseconds * kNanosecondsPerMicrosecond);
}

double Analytics::NanosecondsToMilliseconds(const std::int64_t nanoseconds) {
  return static_cast<double>(nanoseconds) / 1'000'000.0;
}

double Analytics::StandardDeviationNanoseconds(
    const std::vector<std::int64_t>& samples) {
  long double mean{};
  long double squaredDifferenceSum{};
  std::size_t count{};

  for (const auto sample : samples) {
    ++count;
    const long double delta = static_cast<long double>(sample) - mean;
    mean += delta / static_cast<long double>(count);
    squaredDifferenceSum +=
        delta * (static_cast<long double>(sample) - mean);
  }

  return count == 0U
             ? 0.0
             : std::sqrt(static_cast<double>(
                   squaredDifferenceSum / static_cast<long double>(count)));
}

std::int64_t Analytics::Percentile(std::vector<std::int64_t> samples,
                                   const double percentile) {
  if (samples.empty()) {
    return 0;
  }

  std::sort(samples.begin(), samples.end());

  const double clampedPercentile = std::max(0.0, std::min(100.0, percentile));

  const auto index = static_cast<std::size_t>(
      ((clampedPercentile / 100.0) * static_cast<double>(samples.size() - 1U)) +
      0.5);

  return samples[index];
}

void Analytics::WritePlanIdSet(std::ostream& output, const std::string& label,
                               const std::set<std::uint64_t>& planIds) {
  output << label << ": ";

  if (planIds.empty()) {
    output << "none\n";
    return;
  }

  bool first{true};

  for (const auto planId : planIds) {
    if (!first) {
      output << ", ";
    }

    output << planId;
    first = false;
  }

  output << '\n';
}

bool Analytics::LooksLikeDltBinary(const std::string& content) {
  return content.find("DLT") != std::string::npos &&
         content.find('\0') != std::string::npos;
}

std::string Analytics::ConvertDltRecordToTextMessage(const std::string& record,
                                                     std::string& contextId) {
  contextId.clear();

  for (const auto& candidate :
       {"APP", "HLTH", "PLAN", "SYNC", "FSM", "OUT", "DEMO", "ANLY"}) {
    if (record.find(std::string{"CTRL"} + candidate) != std::string::npos) {
      contextId = candidate;
      break;
    }
  }

  if (contextId.empty()) {
    return {};
  }

  if (record.find("Traffic signal demonstration started") !=
      std::string::npos) {
    return "Traffic signal demonstration started";
  }

  if (record.find("Traffic signal demonstration completed") !=
      std::string::npos) {
    return "Traffic signal demonstration completed";
  }

  if (contextId == "OUT") {
    const std::string phase = ExtractKnownValue(
        record, {"NS_GREEN", "EW_GREEN", "YELLOW", "ALL_RED"});
    const std::uint64_t remaining = ExtractDltUint64(record, "remaining");

    if (phase.empty()) {
      return {};
    }

    std::ostringstream message;
    message << "phase=" << phase << ", remaining=" << remaining << " s";
    return message.str();
  }

  const std::string eventName = ExtractKnownValue(
      record,
      {"PLAN_RECEIVED", "PLAN_VALIDATED", "PLAN_REJECTED", "PLAN_QUEUED",
       "PLAN_PUBLISHED", "PLAN_CONSUMED", "PLAN_APPLIED", "EMERGENCY_QUEUED",
       "EMERGENCY_CONSUMED", "EMERGENCY_ACCEPTED", "EMERGENCY_REJECTED",
       "EMERGENCY_APPLIED", "EMERGENCY_DROPPED", "PHASE_ENTER", "PHASE_EXIT",
       "TIMING_DECISION_RECEIVE_TO_CONTROLLER_RECEIVE",
       "PERCEPTION_PUBLISH_TO_CONTROLLER_RECEIVE",
       "EMERGENCY_RECEIVE_TO_APPLY",
       "ANALYTICS_FAILED", "ANALYTICS_REPORT_FAILED",
       "ANALYTICS_REPORT_WRITTEN", "FSM_WAKEUP", "FSM_EXECUTION"});

  if (eventName.empty()) {
    return {};
  }

  std::ostringstream message;
  message << "event=" << eventName;

  if (record.find("plan_id=") != std::string::npos) {
    message << ", plan_id=" << ExtractDltUint64(record, "plan_id");
  }

  const std::string phase =
      ExtractKnownValue(record, {"NS_GREEN", "EW_GREEN", "YELLOW", "ALL_RED"});

  if (!phase.empty()) {
    message << ", phase=" << phase;
  }

  if (record.find("remaining_ms=") != std::string::npos) {
    message << ", remaining_ms=" << ExtractDltUint64(record, "remaining_ms");
  }

  if (record.find("duration_ms=") != std::string::npos) {
    message << ", duration_ms=" << ExtractDltUint64(record, "duration_ms");
  }

  if (record.find("old_remaining_ms=") != std::string::npos) {
    message << ", old_remaining_ms="
            << ExtractDltUint64(record, "old_remaining_ms");
  }

  if (record.find("new_remaining_ms=") != std::string::npos) {
    message << ", new_remaining_ms="
            << ExtractDltUint64(record, "new_remaining_ms");
  }

  if (record.find("latency_ns=") != std::string::npos) {
    message << ", latency_ns=" << ExtractDltUint64(record, "latency_ns");
  }

  for (const auto* const timestampKey :
       {"deadline_ns", "actual_wakeup_ns", "execution_start_ns",
        "publish_timestamp_ns",
        "receive_timestamp_ns", "perception_publish_timestamp_ns",
        "timing_receive_timestamp_ns",
        "controller_receive_timestamp_ns", "apply_timestamp_ns"}) {
    if (record.find(std::string{timestampKey} + "=") != std::string::npos) {
      message << ", " << timestampKey << '='
              << ExtractDltUint64(record, timestampKey);
    }
  }

  if (record.find("execution_time_ns=") != std::string::npos) {
    message << ", execution_time_ns="
            << ExtractDltUint64(record, "execution_time_ns");
  }

  if (record.find("emergency_ns=") != std::string::npos) {
    message << ", emergency_ns="
            << (ExtractDltBool(record, "emergency_ns") ? "true" : "false");
  }

  if (record.find("emergency_ew=") != std::string::npos) {
    message << ", emergency_ew="
            << (ExtractDltBool(record, "emergency_ew") ? "true" : "false");
  }

  const std::string result = ExtractKnownValue(record, {"SUCCESS", "FAILED"});

  if (!result.empty()) {
    message << ", result=" << result;
  }

  const std::string reason = ExtractKnownValue(
      record, {"TOO_EARLY", "TOO_LATE", "WRONG_DIRECTION", "INVALID_PLAN",
               "INVALID_DIRECTION_FLAGS", "NO_ACTIVE_PLAN", "NOT_GREEN",
               "NOT_GREEN_PHASE"});

  if (!reason.empty()) {
    message << ", reason=" << reason;
  }

  return message.str();
}

std::uint64_t Analytics::ExtractDltUint64(const std::string& record,
                                          const std::string& key) {
  const std::string token = key + "=";
  const auto tokenPosition = record.find(token);

  if (tokenPosition == std::string::npos) {
    return 0U;
  }

  const auto searchStart = tokenPosition + token.size();
  const auto searchEnd = std::min(record.size(), searchStart + 32U);

  for (std::size_t position = searchStart; position < searchEnd; ++position) {
    const unsigned char marker = static_cast<unsigned char>(record[position]);

    if (marker != 'C' && marker != 'D') {
      continue;
    }

    const std::size_t valuePosition = position + 4U;
    const std::size_t valueSize = marker == 'D' ? 8U : 4U;

    if (valuePosition + valueSize > record.size()) {
      continue;
    }

    std::uint64_t value{};

    for (std::size_t byteIndex{}; byteIndex < valueSize; ++byteIndex) {
      value |= static_cast<std::uint64_t>(
                   static_cast<unsigned char>(
                       record[valuePosition + byteIndex]))
               << (byteIndex * 8U);
    }

    return value;
  }

  return 0U;
}

bool Analytics::ExtractDltBool(const std::string& record,
                               const std::string& key) {
  const std::string token = key + "=";
  const auto tokenPosition = record.find(token);

  if (tokenPosition == std::string::npos) {
    return false;
  }

  const auto searchStart = tokenPosition + token.size();
  const auto searchEnd = std::min(record.size(), searchStart + 24U);

  for (std::size_t position = searchStart; position < searchEnd; ++position) {
    if (static_cast<unsigned char>(record[position]) != 0x11U) {
      continue;
    }

    const std::size_t valuePosition = position + 4U;

    return valuePosition < record.size() && record[valuePosition] != '\0';
  }

  return false;
}

std::string Analytics::ExtractKnownValue(
    const std::string& record, const std::vector<std::string>& values) {
  for (const auto& value : values) {
    if (record.find(value) != std::string::npos) {
      return value;
    }
  }

  return {};
}

void Analytics::ComputePlanStatistics() {
  for (const auto& entry : logEntries_) {
    switch (entry.eventType) {
      case EventType::PlanReceived:
        ++planStatistics_.received;
        planStatistics_.allPlanIds.insert(entry.planId);

        if (entry.emergencyNs || entry.emergencyEw) {
          planStatistics_.emergencyPlanIds.insert(entry.planId);
        } else {
          planStatistics_.normalPlanIds.insert(entry.planId);
        }

        break;

      case EventType::PlanValidated:
        ++planStatistics_.validated;
        break;

      case EventType::PlanRejected:
        ++planStatistics_.rejected;
        break;

      case EventType::PlanQueued:
        ++planStatistics_.queued;
        break;

      case EventType::PlanPublished:
        ++planStatistics_.published;
        break;

      case EventType::PlanConsumed:
        ++planStatistics_.consumed;
        break;

      case EventType::PlanApplied:
        ++planStatistics_.applied;
        planStatistics_.appliedPlanIds.insert(entry.planId);
        break;

      case EventType::PerceptionToControllerLatency:
      case EventType::DecisionToControllerLatency:
      case EventType::EmergencyReceiveToApply: {
        std::uint64_t latencyNs{};
        if (!ParseUint64(ExtractValue(entry.message, "latency_ns"),
                         latencyNs) ||
            latencyNs > static_cast<std::uint64_t>(
                            std::numeric_limits<std::int64_t>::max())) {
          break;
        }

        if (entry.eventType == EventType::PerceptionToControllerLatency) {
          planStatistics_.perceptionToControllerLatencyNs.push_back(
              static_cast<std::int64_t>(latencyNs));
        } else if (entry.eventType == EventType::DecisionToControllerLatency) {
          planStatistics_.decisionToControllerLatencyNs.push_back(
              static_cast<std::int64_t>(latencyNs));
        } else {
          planStatistics_.emergencyReceiveToApplyLatencySamplesNs.push_back(
              static_cast<std::int64_t>(latencyNs));
        }
        break;
      }

      default:
        break;
    }

    if (entry.eventType == EventType::EmergencyAccepted) {
      planStatistics_.acceptedEmergencyPlanIds.insert(entry.planId);
    }

    if (entry.eventType == EventType::EmergencyRejected) {
      planStatistics_.rejectedEmergencyPlanIds.insert(entry.planId);
    }
  }
}

void Analytics::ComputeEmergencyStatistics() {
  for (const auto& entry : logEntries_) {
    switch (entry.eventType) {
      case EventType::EmergencyQueued:
        ++emergencyStatistics_.queued;
        break;

      case EventType::EmergencyConsumed:
        ++emergencyStatistics_.consumed;
        break;

      case EventType::EmergencyAccepted:
        ++emergencyStatistics_.accepted;
        break;

      case EventType::EmergencyRejected:
        ++emergencyStatistics_.rejected;

        if (!entry.rejectReason.empty()) {
          ++emergencyStatistics_.rejectionReasons[entry.rejectReason];
        }

        break;

      case EventType::EmergencyApplied:
        ++emergencyStatistics_.applied;
        break;

      default:
        break;
    }
  }
}

void Analytics::ComputeWakeupStatistics() {
  for (const auto& entry : logEntries_) {
    if (ExtractValue(entry.message, "event") != "FSM_WAKEUP") {
      continue;
    }

    const std::string latencyValue = ExtractValue(entry.message, "latency_ns");
    std::uint64_t latencyNs{};

    if (!ParseUint64(latencyValue, latencyNs)) {
      continue;
    }

    if (latencyNs >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      continue;
    }

    wakeupStatistics_.latencySamplesNs.push_back(
        static_cast<std::int64_t>(latencyNs));
  }
}

void Analytics::ComputeDisplayStatistics() {
  struct PhaseDisplayStart {
    std::string phase;
    std::uint64_t startedAtNs{};
    std::uint64_t configuredDurationNs{};
  };

  std::vector<PhaseDisplayStart> phaseStarts;
  std::uint64_t lastTimestampNs{};

  for (const auto& entry : logEntries_) {
    if (entry.timestampNs > lastTimestampNs) {
      lastTimestampNs = entry.timestampNs;
    }

    if (entry.eventType != EventType::PhaseEnter || entry.timestampNs == 0U) {
      continue;
    }

    const std::string phase = ExtractValue(entry.message, "phase");
    if (phase.empty()) {
      continue;
    }

    const std::uint64_t durationMs =
        ParseUint64OrZero(ExtractValue(entry.message, "duration_ms"));

    phaseStarts.push_back(
        PhaseDisplayStart{phase, entry.timestampNs,
                          durationMs * kNanosecondsPerMillisecond});
  }

  if (phaseStarts.empty() || lastTimestampNs == 0U) {
    return;
  }

  for (std::size_t index{}; index < phaseStarts.size(); ++index) {
    const auto& current = phaseStarts[index];
    const std::uint64_t nextTimestampNs =
        index + 1U < phaseStarts.size() ? phaseStarts[index + 1U].startedAtNs
                                        : lastTimestampNs;

    if (nextTimestampNs <= current.startedAtNs) {
      continue;
    }

    std::uint64_t displayedNs = nextTimestampNs - current.startedAtNs;
    if (current.configuredDurationNs > 0U) {
      displayedNs = std::min(displayedNs, current.configuredDurationNs);
    }

    displayStatistics_.available = true;
    displayStatistics_.totalDisplayedNs += displayedNs;
    displayStatistics_.phaseDisplayedNs[current.phase] += displayedNs;
  }
}

bool Analytics::ArchiveInputData(const std::string& outputDirectory,
                                 std::string& archivedInputPath) const {
  if (!EnsureDirectoryExists(outputDirectory)) {
    return false;
  }

  const std::string convertedLogPath = logFilePath_ + ".txt";
  const std::string sourcePath =
      FileExistsAndIsNotEmpty(convertedLogPath) ? convertedLogPath
                                                : logFilePath_;

  if (!FileExistsAndIsNotEmpty(sourcePath)) {
    std::cerr << "Unable to archive analytics input; file not found: "
              << sourcePath << '\n';
    return false;
  }

  const std::uint32_t archiveNumber = NextArchiveNumber(outputDirectory);
  const bool sourceIsDlt =
      sourcePath.size() >= 4U &&
      sourcePath.compare(sourcePath.size() - 4U, 4U, ".dlt") == 0;

  std::ostringstream archiveName;
  archiveName << std::setw(3) << std::setfill('0') << archiveNumber
              << "_analytics_input" << (sourceIsDlt ? ".dlt" : ".txt");

  archivedInputPath = outputDirectory + "/" + archiveName.str();

  return CopyFile(sourcePath, archivedInputPath);
}

bool Analytics::WriteReport(const std::string& outputPath) const {
  std::ofstream output(outputPath);

  if (!output.is_open()) {
    std::cerr << "Unable to open report file: " << outputPath << '\n';

    return false;
  }

  output << "TRAFFIC SIGNAL ANALYTICS REPORT\n";
  output << "===============================\n\n";

  output << "Log entries: " << logEntries_.size() << "\n\n";

  output << "DISPLAY DURATION\n";
  output << "----------------\n";

  if (displayStatistics_.available) {
    output << "Total displayed time: "
           << FormatNanoseconds(displayStatistics_.totalDisplayedNs) << '\n';

    for (const auto* const phase :
         {"NS_GREEN", "EW_GREEN", "YELLOW", "ALL_RED"}) {
      const auto phaseIterator =
          displayStatistics_.phaseDisplayedNs.find(phase);

      if (phaseIterator == displayStatistics_.phaseDisplayedNs.end()) {
        continue;
      }

      output << phase << ": " << FormatNanoseconds(phaseIterator->second)
             << '\n';
    }

    output << '\n';
  } else {
    output << "Total displayed time: unavailable "
              "(no phase enter log entries)\n\n";
  }

  output << "PLAN STATISTICS\n";
  output << "---------------\n";
  output << "Received: " << planStatistics_.received << '\n';
  output << "Validated: " << planStatistics_.validated << '\n';
  output << "Rejected: " << planStatistics_.rejected << '\n';
  output << "Queued: " << planStatistics_.queued << '\n';
  output << "Published: " << planStatistics_.published << '\n';
  output << "Consumed: " << planStatistics_.consumed << '\n';
  output << "Applied: " << planStatistics_.applied << '\n';

  const auto writeLatencyStatistics = [&output](
      const std::string& title,
      const std::vector<std::int64_t>& samples) {
    output << '\n' << title << "\n";
    output << std::string(title.size(), '-') << "\n";
    output << "Samples: " << samples.size() << '\n';

    if (samples.empty()) {
      output << "Latency: unavailable (no valid samples)\n";
      return;
    }

    const auto [minimumIterator, maximumIterator] =
        std::minmax_element(samples.begin(), samples.end());
    long double totalLatencyNs{};
    for (const auto latencyNs : samples) {
      totalLatencyNs += latencyNs;
    }

    const long double averageLatencyNs =
        totalLatencyNs / static_cast<long double>(samples.size());
    output << std::fixed << std::setprecision(3);
    output << "Min latency: " << FormatNanoseconds(*minimumIterator) << '\n';
    output << "Max latency: " << FormatNanoseconds(*maximumIterator) << '\n';
    output << "Average latency: " << FormatNanoseconds(averageLatencyNs)
           << '\n';
    output << "Standard deviation: "
           << FormatNanoseconds(
                  Analytics::StandardDeviationNanoseconds(samples))
           << '\n';
    output << "P50 latency: "
           << FormatNanoseconds(Analytics::Percentile(samples, 50.0)) << '\n';
    output << "P90 latency: "
           << FormatNanoseconds(Analytics::Percentile(samples, 90.0)) << '\n';
    output << "P95 latency: "
           << FormatNanoseconds(Analytics::Percentile(samples, 95.0)) << '\n';
    output << "P99 latency: "
           << FormatNanoseconds(Analytics::Percentile(samples, 99.0)) << '\n';
  };

  writeLatencyStatistics(
      "PERCEPTION PUBLISH-TO-CONTROLLER RECEIVE LATENCY",
      planStatistics_.perceptionToControllerLatencyNs);
  writeLatencyStatistics(
      "TIMING DECISION RECEIVE-TO-CONTROLLER RECEIVE LATENCY",
      planStatistics_.decisionToControllerLatencyNs);
  writeLatencyStatistics(
      "EMERGENCY RECEIVE-TO-APPLY LATENCY",
      planStatistics_.emergencyReceiveToApplyLatencySamplesNs);

  output << "\nEMERGENCY STATISTICS\n";
  output << "--------------------\n";
  output << "Queued: " << emergencyStatistics_.queued << '\n';
  output << "Consumed: " << emergencyStatistics_.consumed << '\n';
  output << "Accepted: " << emergencyStatistics_.accepted << '\n';
  output << "Rejected: " << emergencyStatistics_.rejected << '\n';
  output << "Applied: " << emergencyStatistics_.applied << '\n';

  output << "\nRejection reasons:\n";

  for (const auto& [reason, count] : emergencyStatistics_.rejectionReasons) {
    output << "  " << reason << ": " << count << '\n';
  }

  output << "\nFSM WAKEUP LATENCY\n";
  output << "------------------\n";
  output << "Samples: " << wakeupStatistics_.latencySamplesNs.size() << '\n';

  if (!wakeupStatistics_.latencySamplesNs.empty()) {
    const auto [minimumIterator, maximumIterator] =
        std::minmax_element(wakeupStatistics_.latencySamplesNs.begin(),
                            wakeupStatistics_.latencySamplesNs.end());

    long double totalLatencyNs{};

    for (const auto latencyNs : wakeupStatistics_.latencySamplesNs) {
      totalLatencyNs += latencyNs;
    }

    const long double averageLatencyNs =
        totalLatencyNs /
            static_cast<long double>(
                wakeupStatistics_.latencySamplesNs.size());

    output << std::fixed << std::setprecision(3);
    output << "Min latency: " << FormatNanoseconds(*minimumIterator) << '\n';
    output << "Max latency: " << FormatNanoseconds(*maximumIterator) << '\n';
    output << "Average latency: " << FormatNanoseconds(averageLatencyNs)
           << '\n';
    output << "Standard deviation: "
           << FormatNanoseconds(StandardDeviationNanoseconds(
                  wakeupStatistics_.latencySamplesNs))
           << '\n';
    output << "P50 latency: "
           << FormatNanoseconds(
                  Percentile(wakeupStatistics_.latencySamplesNs, 50.0))
           << '\n';
    output << "P90 latency: "
           << FormatNanoseconds(
                  Percentile(wakeupStatistics_.latencySamplesNs, 90.0))
           << '\n';
    output << "P95 latency: "
           << FormatNanoseconds(
                  Percentile(wakeupStatistics_.latencySamplesNs, 95.0))
           << '\n';
    output << "P99 latency: "
           << FormatNanoseconds(
                  Percentile(wakeupStatistics_.latencySamplesNs, 99.0))
           << '\n';
  }

  output << "\nFSM EXECUTION TIME\n";
  output << "------------------\n";

  std::vector<std::int64_t> executionSamplesNs;

  for (const auto& entry : logEntries_) {
    if (ExtractValue(entry.message, "event") != "FSM_EXECUTION") {
      continue;
    }

    const std::string executionValue =
        ExtractValue(entry.message, "execution_time_ns");
    std::uint64_t executionTimeNs{};

    if (!ParseUint64(executionValue, executionTimeNs)) {
      continue;
    }

    if (executionTimeNs >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      continue;
    }

    executionSamplesNs.push_back(
        static_cast<std::int64_t>(executionTimeNs));
  }

  output << "Samples: " << executionSamplesNs.size() << '\n';

  if (!executionSamplesNs.empty()) {
    const auto [minimumIterator, maximumIterator] =
        std::minmax_element(executionSamplesNs.begin(),
                            executionSamplesNs.end());

    long double totalExecutionTimeNs{};

    for (const auto executionTimeNs : executionSamplesNs) {
      totalExecutionTimeNs += executionTimeNs;
    }

    const long double averageExecutionTimeNs =
        totalExecutionTimeNs /
        static_cast<long double>(executionSamplesNs.size());

    output << std::fixed << std::setprecision(3);
    output << "Min execution time: "
           << FormatNanoseconds(*minimumIterator) << '\n';
    output << "Max execution time: "
           << FormatNanoseconds(*maximumIterator) << '\n';
    output << "Average execution time: "
           << FormatNanoseconds(averageExecutionTimeNs) << '\n';
    const double executionStandardDeviationNs =
        StandardDeviationNanoseconds(executionSamplesNs);
    output << "Standard deviation: "
           << FormatNanoseconds(executionStandardDeviationNs) << '\n';
    output << "P50 execution time: "
           << FormatNanoseconds(Percentile(executionSamplesNs, 50.0)) << '\n';
    output << "P90 execution time: "
           << FormatNanoseconds(Percentile(executionSamplesNs, 90.0)) << '\n';
    output << "P95 execution time: "
           << FormatNanoseconds(Percentile(executionSamplesNs, 95.0)) << '\n';
    output << "P99 execution time: "
           << FormatNanoseconds(Percentile(executionSamplesNs, 99.0)) << '\n';
  }

  return true;
}
