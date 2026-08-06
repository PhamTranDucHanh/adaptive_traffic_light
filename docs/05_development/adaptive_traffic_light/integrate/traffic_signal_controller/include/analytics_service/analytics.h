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
  bool ArchiveInputData(
      const std::string& outputDirectory,
      std::string& archivedInputPath) const;

 private:
  enum class EventType {
    Unknown,

    PhaseEnter,

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
    EmergencyApplied,

    PlanPublishToReceive,
    EmergencyReceiveToApply
  };

  struct LogEntry {
    std::uint64_t timestampNs{};

    std::string message;
    EventType eventType{EventType::Unknown};

    std::uint64_t planId{};

    bool emergencyNs{};
    bool emergencyEw{};

    std::string rejectReason;
  };

  struct PlanStatistics {
    std::uint32_t received{};
    std::uint32_t validated{};
    std::uint32_t rejected{};
    std::uint32_t queued{};
    std::uint32_t published{};
    std::uint32_t consumed{};
    std::uint32_t applied{};

    std::vector<std::int64_t> publishToReceiveLatencySamplesNs;
    std::vector<std::int64_t> emergencyReceiveToApplyLatencySamplesNs;

    std::set<std::uint64_t> allPlanIds;
    std::set<std::uint64_t> normalPlanIds;
    std::set<std::uint64_t> emergencyPlanIds;
    std::set<std::uint64_t> appliedPlanIds;
    std::set<std::uint64_t> acceptedEmergencyPlanIds;
    std::set<std::uint64_t> rejectedEmergencyPlanIds;
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

  struct DisplayStatistics {
    bool available{};
    std::uint64_t totalDisplayedNs{};
    std::unordered_map<std::string, std::uint64_t> phaseDisplayedNs;
  };

  bool LoadLog();
  bool LoadDltBinaryLog(const std::string& content);
  bool ParseLogLine(const std::string& line, LogEntry& entry) const;
  void ParseApplicationMessage(LogEntry& entry) const;

  void ComputeDisplayStatistics();
  void ComputePlanStatistics();
  void ComputeEmergencyStatistics();
  void ComputeWakeupStatistics();

  static EventType EventTypeFromString(const std::string& eventName);

  static std::string ExtractValue(
      const std::string& message,
      const std::string& key);

  static std::uint64_t ParseUint64OrZero(
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
      const std::set<std::uint64_t>& planIds);

  static bool LooksLikeDltBinary(const std::string& content);
  static std::string ConvertDltRecordToTextMessage(
      const std::string& record,
      std::string& contextId);
  static std::uint64_t ExtractDltUint64(
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

  PlanStatistics planStatistics_;
  EmergencyStatistics emergencyStatistics_;
  WakeupStatistics wakeupStatistics_;
  DisplayStatistics displayStatistics_;
};

#endif
