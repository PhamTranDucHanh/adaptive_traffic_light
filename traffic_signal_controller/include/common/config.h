#ifndef CONFIG_H
#define CONFIG_H

#include <array>
#include <cstdint>
#include <string>

constexpr uint8_t MAX_PHASES = 8;
constexpr uint16_t MAX_SAMPLES = 512;

constexpr uint32_t TIMER_INTERVAL_MS = 100;

enum class PhaseId : uint8_t { NS_GREEN, YELLOW, ALL_RED, EW_GREEN };

struct Phase {
  PhaseId phaseId{};
  uint32_t durationMs{};
};

struct TimerTick {
  uint64_t theoreticalTimestamp{};
  uint64_t sentTimestamp{};
};

struct PlanData {
  uint64_t sourcePlanId{};

  std::array<Phase, MAX_PHASES> phases{};
  uint8_t phaseCount{};

  uint32_t totalCycleMs{};

  bool isEmergencyNS{};
  bool isEmergencyEW{};

  uint64_t receivedAt{};
};

struct SignalOutput {
  PhaseId phaseId{};

  uint32_t remainingMs{};

  bool isEmergency{};

  uint64_t theoreticalTimestamp{};

  uint64_t decrementStartAt{};
  uint64_t planAppliedAt{};
  uint64_t planReceivedAt{};

  bool hasNewPlan{};
};

struct DataCollect {
  uint64_t cycleId{};
  uint64_t sourcePlanId{};

  uint32_t nsGreenDurationMs{};
  uint32_t ewGreenDurationMs{};
  uint32_t yellowDurationMs{};
  uint32_t allRedDurationMs{};

  bool isEmergencyCycle{};

  std::array<SignalOutput, MAX_SAMPLES> samples{};

  uint8_t sampleCount{};
};

struct AlertMetrics {
  uint64_t cycleId{};

  int64_t latencyNs{};

  bool hasEmergencyMeasurement{};
  int64_t responseLatencyNs{};

  bool isEmergencyCycle{};
};

struct AlertNotification {
  uint64_t cycleId{};
  int64_t violatedValue{};
  int64_t thresholdValue{};
  uint64_t detectedAt{};
};

struct SignalDisplay {
  PhaseId phaseId{};
  uint32_t remainingTimeMs{};
};

struct LogEntry {
  DataCollect cycleData{};
  uint64_t loggedTimestamp{};
};

struct DashboardData {
  uint64_t cycleId{};
  int64_t avgLatencyNs{};

  uint32_t nsGreenDurationMs{};
  uint32_t ewGreenDurationMs{};
  uint32_t yellowDurationMs{};
  uint32_t allRedDurationMs{};

  bool isEmergencyCycle{};

  uint64_t totalCyclesProcessed{};
};

struct DashboardView {
  DashboardData data{};
  uint64_t refreshedAt{};
};

struct ReportMetrics {
  uint64_t cycleId{};

  uint32_t nsGreenDuration{};
  uint32_t ewGreenDuration{};
  uint32_t yellowDuration{};
  uint32_t allRedDuration{};

  bool isEmergencyCycle{};

  int64_t avgLatencyNs{};
  int64_t maxLatencyNs{};
  int64_t minLatencyNs{};
};

struct ReportData {
  uint64_t generatedAt{};
  ReportMetrics metrics{};
};

struct HealthStatus {
  uint64_t cycleId{};

  bool timerAlive{};
  bool fsmAlive{};
  bool publisherAlive{};
  bool loggerAlive{};
  bool analyticsAlive{};

  uint64_t lastHeartbeat{};
};

#endif