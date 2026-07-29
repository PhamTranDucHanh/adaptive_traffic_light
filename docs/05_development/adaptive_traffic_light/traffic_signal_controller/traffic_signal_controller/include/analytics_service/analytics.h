#ifndef ANALYTICS_H
#define ANALYTICS_H

#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class Analytics {
 public:
  explicit Analytics(std::string logFilePath);

  bool Analyze();
  bool WriteReport(const std::string& outputPath) const;

 private:
  enum class EventType {
    Unknown,

    DemoStarted,
    DemoCompleted,

    PhaseStatus,
    PhaseEnter,
    PhaseExit,

    PlanReceived,
    PlanValidated,
    PlanRejected,
    PlanQueued,
    PlanPublished,
    PlanConsumed,
    PlanApplied,

    EmergencyQueued,
    EmergencyConsumed,
    EmergencyAccepted,
    EmergencyRejected,
    EmergencyApplied
  };

  struct LogEntry {
    std::string date;
    std::string time;

    std::string ecuId;
    std::string applicationId;
    std::string contextId;
    std::string logLevel;

    double dltTimestampSeconds{};
    std::uint64_t timestampNs{};

    std::string message;
    EventType eventType{EventType::Unknown};

    std::uint32_t planId{};
    std::string phase;

    std::uint32_t remainingTimeMs{};
    std::uint32_t durationMs{};
    std::uint32_t oldRemainingTimeMs{};
    std::uint32_t newRemainingTimeMs{};

    bool emergencyNs{};
    bool emergencyEw{};

    std::string result;
    std::string rejectReason;
  };

  struct PhaseStatistics {
    std::uint32_t entryCount{};
    std::uint64_t totalConfiguredDurationMs{};
  };

  struct PlanStatistics {
    std::uint32_t received{};
    std::uint32_t validated{};
    std::uint32_t rejected{};
    std::uint32_t queued{};
    std::uint32_t published{};
    std::uint32_t consumed{};
    std::uint32_t applied{};

    std::uint64_t totalReceiveToApplyLatencyNs{};
    std::uint32_t latencySampleCount{};

    std::set<std::uint32_t> allPlanIds;
    std::set<std::uint32_t> normalPlanIds;
    std::set<std::uint32_t> emergencyPlanIds;
    std::set<std::uint32_t> appliedPlanIds;
    std::set<std::uint32_t> acceptedEmergencyPlanIds;
    std::set<std::uint32_t> rejectedEmergencyPlanIds;
  };

  struct EmergencyStatistics {
    std::uint32_t queued{};
    std::uint32_t consumed{};
    std::uint32_t accepted{};
    std::uint32_t rejected{};
    std::uint32_t applied{};

    std::unordered_map<std::string, std::uint32_t> rejectionReasons;
  };

  struct WakeupStatistics {
    std::vector<std::int64_t> latencySamplesNs;
  };

  bool LoadLog();
  bool LoadDltBinaryLog(const std::string& content);
  bool ParseLogLine(const std::string& line, LogEntry& entry) const;
  void ParseApplicationMessage(LogEntry& entry) const;

  void ComputePhaseStatistics();
  void ComputePlanStatistics();
  void ComputeEmergencyStatistics();
  void ComputeWakeupStatistics();

  static EventType EventTypeFromString(const std::string& eventName);

  static std::string ExtractValue(
      const std::string& message,
      const std::string& key);

  static std::uint32_t ParseUint32(
      const std::string& value);

  static bool ParseBool(
      const std::string& value);

  static std::uint64_t ExtractDltStorageTimestampNs(
      const std::string& record);
  static double NanosecondsToMilliseconds(std::int64_t nanoseconds);
  static std::int64_t Percentile(
      std::vector<std::int64_t> samples,
      double percentile);
  static void WritePlanIdSet(
      std::ostream& output,
      const std::string& label,
      const std::set<std::uint32_t>& planIds);

  static bool LooksLikeDltBinary(const std::string& content);
  static std::string ConvertDltRecordToTextMessage(
      const std::string& record,
      std::string& contextId);
  static std::uint32_t ExtractDltUint32(
      const std::string& record,
      const std::string& key);
  static bool ExtractDltBool(
      const std::string& record,
      const std::string& key);
  static std::string ExtractKnownValue(
      const std::string& record,
      const std::vector<std::string>& values);

  std::string logFilePath_;
  std::vector<LogEntry> logEntries_;

  std::unordered_map<std::string, PhaseStatistics> phaseStatistics_;
  PlanStatistics planStatistics_;
  EmergencyStatistics emergencyStatistics_;
  WakeupStatistics wakeupStatistics_;
};

#endif
