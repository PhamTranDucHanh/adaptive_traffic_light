#include "analytics_service/analytics.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

std::string Trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");

  if (first == std::string::npos) {
    return {};
  }

  const auto last = value.find_last_not_of(" \t\r\n");

  return value.substr(first, last - first + 1U);
}

bool ParseUint64(const std::string& value, std::uint64_t& result) noexcept {
  if (value.empty()) {
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

}  // namespace

Analytics::Analytics(std::string logFilePath)
    : logFilePath_(std::move(logFilePath)) {}

bool Analytics::Analyze() {
  logEntries_.clear();
  planStatistics_ = {};
  emergencyStatistics_ = {};
  wakeupStatistics_ = {};

  if (!LoadLog()) {
    return false;
  }

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
      entry.applicationId = "CTRL";
      entry.contextId = contextId;
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

  std::string dltTimestamp;
  std::string messageCounter;
  std::string messageType;
  std::string verbose;
  std::string argumentCount;

  if (!(stream >> entry.date >> entry.time >> dltTimestamp >> messageCounter >>
        messageType >> entry.ecuId >> entry.applicationId >> entry.contextId >>
        messageType >> entry.logLevel >> verbose >> argumentCount)) {
    return false;
  }

  try {
    entry.dltTimestampSeconds = std::stod(dltTimestamp);
  } catch (...) {
    entry.dltTimestampSeconds = 0.0;
  }

  entry.timestampNs =
      static_cast<std::uint64_t>(entry.dltTimestampSeconds * 1'000'000'000.0);

  std::getline(stream, entry.message);
  entry.message = Trim(entry.message);

  return !entry.message.empty();
}

void Analytics::ParseApplicationMessage(LogEntry& entry) const {
  if (entry.message == "Traffic signal demonstration started") {
    entry.eventType = EventType::DemoStarted;
    return;
  }

  if (entry.message == "Traffic signal demonstration completed") {
    entry.eventType = EventType::DemoCompleted;
    return;
  }

  if (entry.contextId == "OUT") {
    const std::string phase = ExtractValue(entry.message, "phase");

    // Chỉ xem đây là trạng thái output khi message thật sự có phase.
    // Các log lỗi/sự kiện khác của OUT vẫn được parse theo event= bên dưới.
    if (!phase.empty()) {
      entry.eventType = EventType::PhaseStatus;
      entry.phase = phase;

      const auto remainingSeconds =
          ParseUint32(ExtractValue(entry.message, "remaining"));

      entry.remainingTimeMs = remainingSeconds * 1000U;
      return;
    }
  }

  const std::string eventName = ExtractValue(entry.message, "event");

  entry.eventType = EventTypeFromString(eventName);

  entry.planId = ParseUint32(ExtractValue(entry.message, "plan_id"));

  entry.phase = ExtractValue(entry.message, "phase");

  entry.remainingTimeMs =
      ParseUint32(ExtractValue(entry.message, "remaining_ms"));

  entry.durationMs = ParseUint32(ExtractValue(entry.message, "duration_ms"));

  entry.oldRemainingTimeMs =
      ParseUint32(ExtractValue(entry.message, "old_remaining_ms"));

  entry.newRemainingTimeMs =
      ParseUint32(ExtractValue(entry.message, "new_remaining_ms"));

  entry.emergencyNs = ParseBool(ExtractValue(entry.message, "emergency_ns"));

  entry.emergencyEw = ParseBool(ExtractValue(entry.message, "emergency_ew"));

  entry.result = ExtractValue(entry.message, "result");

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

      {"PHASE_ENTER", EventType::PhaseEnter},
      {"PHASE_EXIT", EventType::PhaseExit}};

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

std::uint32_t Analytics::ParseUint32(const std::string& value) {
  if (value.empty()) {
    return 0U;
  }

  try {
    std::size_t processedCharacters{};
    const auto result = std::stoul(value, &processedCharacters);

    return static_cast<std::uint32_t>(result);
  } catch (...) {
    return 0U;
  }
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

  constexpr std::uint64_t kNanosecondsPerSecond{1'000'000'000ULL};
  constexpr std::uint64_t kNanosecondsPerMicrosecond{1'000ULL};

  const std::uint64_t seconds = readLittleEndianUint32(4U);
  const std::uint64_t microseconds = readLittleEndianUint32(8U);

  return (seconds * kNanosecondsPerSecond) +
         (microseconds * kNanosecondsPerMicrosecond);
}

double Analytics::NanosecondsToMilliseconds(const std::int64_t nanoseconds) {
  return static_cast<double>(nanoseconds) / 1'000'000.0;
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
                               const std::set<std::uint32_t>& planIds) {
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
    const std::uint32_t remaining = ExtractDltUint32(record, "remaining");

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
       "ANALYTICS_FAILED", "ANALYTICS_REPORT_FAILED",
       "ANALYTICS_REPORT_WRITTEN", "FSM_WAKEUP"});

  if (eventName.empty()) {
    return {};
  }

  std::ostringstream message;
  message << "event=" << eventName;

  if (record.find("plan_id=") != std::string::npos) {
    message << ", plan_id=" << ExtractDltUint32(record, "plan_id");
  }

  const std::string phase =
      ExtractKnownValue(record, {"NS_GREEN", "EW_GREEN", "YELLOW", "ALL_RED"});

  if (!phase.empty()) {
    message << ", phase=" << phase;
  }

  if (record.find("remaining_ms=") != std::string::npos) {
    message << ", remaining_ms=" << ExtractDltUint32(record, "remaining_ms");
  }

  if (record.find("duration_ms=") != std::string::npos) {
    message << ", duration_ms=" << ExtractDltUint32(record, "duration_ms");
  }

  if (record.find("old_remaining_ms=") != std::string::npos) {
    message << ", old_remaining_ms="
            << ExtractDltUint32(record, "old_remaining_ms");
  }

  if (record.find("new_remaining_ms=") != std::string::npos) {
    message << ", new_remaining_ms="
            << ExtractDltUint32(record, "new_remaining_ms");
  }

  if (record.find("latency_ns=") != std::string::npos) {
    message << ", latency_ns=" << ExtractDltUint32(record, "latency_ns");
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

std::uint32_t Analytics::ExtractDltUint32(const std::string& record,
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

    if (valuePosition + 4U > record.size()) {
      continue;
    }

    return static_cast<std::uint32_t>(
        static_cast<unsigned char>(record[valuePosition]) |
        (static_cast<std::uint32_t>(
             static_cast<unsigned char>(record[valuePosition + 1U]))
         << 8U) |
        (static_cast<std::uint32_t>(
             static_cast<unsigned char>(record[valuePosition + 2U]))
         << 16U) |
        (static_cast<std::uint32_t>(
             static_cast<unsigned char>(record[valuePosition + 3U]))
         << 24U));
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
  std::unordered_map<std::uint32_t, std::uint64_t> receivedTimestamps;

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

        receivedTimestamps[entry.planId] = entry.timestampNs;
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

      case EventType::PlanApplied: {
        ++planStatistics_.applied;
        planStatistics_.appliedPlanIds.insert(entry.planId);

        const auto receivedIterator = receivedTimestamps.find(entry.planId);

        if (receivedIterator != receivedTimestamps.end()) {
          const auto latency = entry.timestampNs - receivedIterator->second;

          planStatistics_.totalReceiveToApplyLatencyNs += latency;

          ++planStatistics_.latencySampleCount;
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

bool Analytics::WriteReport(const std::string& outputPath) const {
  std::ofstream output(outputPath);

  if (!output.is_open()) {
    std::cerr << "Unable to open report file: " << outputPath << '\n';

    return false;
  }

  output << "TRAFFIC SIGNAL ANALYTICS REPORT\n";
  output << "===============================\n\n";

  output << "Log entries: " << logEntries_.size() << "\n\n";

  output << "PLAN STATISTICS\n";
  output << "---------------\n";
  output << "Received: " << planStatistics_.received << '\n';
  output << "Validated: " << planStatistics_.validated << '\n';
  output << "Rejected: " << planStatistics_.rejected << '\n';
  output << "Queued: " << planStatistics_.queued << '\n';
  output << "Published: " << planStatistics_.published << '\n';
  output << "Consumed: " << planStatistics_.consumed << '\n';
  output << "Applied: " << planStatistics_.applied << '\n';

  if (planStatistics_.latencySampleCount > 0U) {
    const double averageLatencyMs =
        static_cast<double>(planStatistics_.totalReceiveToApplyLatencyNs) /
        static_cast<double>(planStatistics_.latencySampleCount) / 1'000'000.0;

    output << "Average receive-to-apply latency: " << std::fixed
           << std::setprecision(3) << averageLatencyMs << " ms\n";
  }

  output << "\nPLAN INVENTORY\n";
  output << "--------------\n";
  WritePlanIdSet(output, "All plans seen", planStatistics_.allPlanIds);
  WritePlanIdSet(output, "Normal plans", planStatistics_.normalPlanIds);
  WritePlanIdSet(output, "Emergency plans", planStatistics_.emergencyPlanIds);
  WritePlanIdSet(output, "Normal plans applied",
                 planStatistics_.appliedPlanIds);
  WritePlanIdSet(output, "Emergency plans accepted",
                 planStatistics_.acceptedEmergencyPlanIds);
  WritePlanIdSet(output, "Emergency plans rejected",
                 planStatistics_.rejectedEmergencyPlanIds);

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

    std::int64_t totalLatencyNs{};

    for (const auto latencyNs : wakeupStatistics_.latencySamplesNs) {
      totalLatencyNs += latencyNs;
    }

    const double averageLatencyMs =
        NanosecondsToMilliseconds(totalLatencyNs) /
        static_cast<double>(wakeupStatistics_.latencySamplesNs.size());

    output << std::fixed << std::setprecision(3);
    output << "Min latency: " << NanosecondsToMilliseconds(*minimumIterator)
           << " ms\n";
    output << "Max latency: " << NanosecondsToMilliseconds(*maximumIterator)
           << " ms\n";
    output << "Average latency: " << averageLatencyMs << " ms\n";
    output << "P50 latency: "
           << NanosecondsToMilliseconds(
                  Percentile(wakeupStatistics_.latencySamplesNs, 50.0))
           << " ms\n";
    output << "P90 latency: "
           << NanosecondsToMilliseconds(
                  Percentile(wakeupStatistics_.latencySamplesNs, 90.0))
           << " ms\n";
    output << "P95 latency: "
           << NanosecondsToMilliseconds(
                  Percentile(wakeupStatistics_.latencySamplesNs, 95.0))
           << " ms\n";
    output << "P99 latency: "
           << NanosecondsToMilliseconds(
                  Percentile(wakeupStatistics_.latencySamplesNs, 99.0))
           << " ms\n";
  }

  return true;
}
