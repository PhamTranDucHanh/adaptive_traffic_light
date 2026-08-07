#ifndef TRAFFIC_TIMING_DECISION_DECISION_CONSTANTS_H
#define TRAFFIC_TIMING_DECISION_DECISION_CONSTANTS_H

#include <cstdint>

namespace traffic_timing_decision {

// Integral decision parameters are strongly typed so that their units and
// roles remain explicit at every call site.
enum class TimingMilliseconds : std::uint32_t {
  kAllRed = 1000U,
  kYellow = 3000U,
  kGreenAdjustmentStep = 5000U,
  kMinimumGreen = 10000U,
  kLowDemandGreen = 20000U,
  kInitialGreen = 30000U,
  kModerateDemandGreen = 30000U,
  kHighDemandGreen = 40000U,
  kVeryHighDemandGreen = 50000U,
  kMaximumGreen = 60000U,
  kMaximumCycle = 100000U,
};

constexpr std::uint32_t toMilliseconds(
    const TimingMilliseconds value) noexcept {
  return static_cast<std::uint32_t>(value);
}

enum class IntersectionLayout : std::uint32_t {
  kDirectionalPhaseCount = 2U,
};

constexpr std::uint32_t toCount(const IntersectionLayout value) noexcept {
  return static_cast<std::uint32_t>(value);
}

enum class PlanSequence : std::uint64_t {
  kFirstIdentifier = 1U,
};

constexpr std::uint64_t toIdentifier(const PlanSequence value) noexcept {
  return static_cast<std::uint64_t>(value);
}

// Floating-point values cannot be enum members in C++, so named constexpr
// values are used for score thresholds and coefficients.
inline constexpr float kPairMeanMultiplier = 0.5F;
inline constexpr float kOccupancyPercentageMultiplier = 100.0F;
inline constexpr float kQueueLengthWeight = 0.5F;
inline constexpr float kVehicleCountWeight = 0.3F;
inline constexpr float kOccupancyWeight = 0.2F;
inline constexpr float kLowDemandScoreUpperBound = 30.0F;
inline constexpr float kModerateDemandScoreUpperBound = 60.0F;
inline constexpr float kHighDemandScoreUpperBound = 90.0F;

}  // namespace traffic_timing_decision

#endif  // TRAFFIC_TIMING_DECISION_DECISION_CONSTANTS_H
