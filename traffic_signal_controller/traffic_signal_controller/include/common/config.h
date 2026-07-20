#ifndef TRAFFIC_SIGNAL_CONTROLLER_COMMON_CONFIG_H_
#define TRAFFIC_SIGNAL_CONTROLLER_COMMON_CONFIG_H_

#include <array>
#include <cstdint>

constexpr std::uint8_t MAX_PHASES{6U};
constexpr std::uint32_t TIMER_INTERVAL_MS{100U};

struct TimingPlan {
  std::uint64_t planId{0U};
  std::uint64_t generationTimestampNs{0U};

  std::uint32_t greenNorthSouthMs{0U};
  std::uint32_t greenEastWestMs{0U};
  std::uint32_t yellowMs{3000U};
  std::uint32_t allRedMs{1000U};
  std::uint32_t cycleLengthMs{0U};

  bool emergencyNorthSouth{false};
  bool emergencyEastWest{false};
};

enum class PhaseId : std::uint8_t {
  NS_GREEN,
  YELLOW,
  ALL_RED,
  EW_GREEN,
};

struct Phase {
  PhaseId phaseId{PhaseId::ALL_RED};
  std::uint32_t durationMs{0U};
};

struct PlanData {
  std::uint64_t sourcePlanId{0U};
  std::array<Phase, MAX_PHASES> phases{};
  std::uint8_t phaseCount{0U};
  std::uint32_t totalCycleMs{0U};
  bool isEmergencyNS{false};
  bool isEmergencyEW{false};
  std::uint64_t receivedAt{0U};
};

inline PlanData MakeDefaultPlan() {
  PlanData plan{};

  plan.sourcePlanId = 0U;

  plan.phases[0] = Phase{PhaseId::NS_GREEN, 30'000U};

  plan.phases[1] = Phase{PhaseId::YELLOW, 3'000U};

  plan.phases[2] = Phase{PhaseId::ALL_RED, 1'000U};

  plan.phases[3] = Phase{PhaseId::EW_GREEN, 30'000U};

  plan.phases[4] = Phase{PhaseId::YELLOW, 3'000U};

  plan.phases[5] = Phase{PhaseId::ALL_RED, 1'000U};

  plan.phaseCount = 6U;

  plan.totalCycleMs = plan.phases[0].durationMs + plan.phases[1].durationMs +
                      plan.phases[2].durationMs + plan.phases[3].durationMs +
                      plan.phases[4].durationMs + plan.phases[5].durationMs;

  plan.isEmergencyNS = false;
  plan.isEmergencyEW = false;
  plan.receivedAt = 0U;

  return plan;
}
struct SignalDisplay {
  PhaseId phaseId{PhaseId::ALL_RED};
  std::uint32_t remainingTimeMs{0U};
};

struct HealthStatus {
  uint64_t cycleId{};

  bool fsmAlive{};
  bool loggerAlive{};
  bool analyticsAlive{};

  uint64_t lastHeartbeat{};
};

#endif  // TRAFFIC_SIGNAL_CONTROLLER_COMMON_CONFIG_H_
