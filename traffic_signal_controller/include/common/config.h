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


struct PlanData {
  uint64_t sourcePlanId{};

  std::array<Phase, MAX_PHASES> phases{};
  uint8_t phaseCount{};

  uint32_t totalCycleMs{};

  bool isEmergencyNS{};
  bool isEmergencyEW{};

  uint64_t receivedAt{};
};


struct SignalDisplay {
  PhaseId phaseId{};
  uint32_t remainingTimeMs{};
};


#endif