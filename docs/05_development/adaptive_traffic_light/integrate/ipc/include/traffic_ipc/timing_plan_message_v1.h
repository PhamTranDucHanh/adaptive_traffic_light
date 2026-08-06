#ifndef TRAFFIC_IPC_TIMING_PLAN_MESSAGE_V1_H
#define TRAFFIC_IPC_TIMING_PLAN_MESSAGE_V1_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace traffic_ipc {

inline constexpr char kTimingPlanQueueName[] = "/traffic_timing_plan_v1";
inline constexpr long kTimingPlanQueueMaxMessages = 8L;
inline constexpr long kTimingPlanQueueMessageSize = 72L;
inline constexpr std::uint32_t kTimingPlanMagic = 0x54504C31U;  // "TPL1"
inline constexpr std::uint16_t kTimingPlanVersion = 1U;

// Stable POSIX-MQ wire contract shared by Timing Decision and Signal Control.
// Boolean values are encoded as uint8_t and are valid only when equal to 0 or
// 1. The final reserved bytes keep the v1 message exactly 72 bytes.
struct TimingPlanMessageV1 final {
  std::uint32_t magic{kTimingPlanMagic};
  std::uint16_t version{kTimingPlanVersion};
  std::uint16_t messageSize{static_cast<std::uint16_t>(
      kTimingPlanQueueMessageSize)};

  std::uint64_t publisherInstanceId{0U};
  std::uint64_t sequenceNumber{0U};
  std::uint64_t planId{0U};
  std::uint64_t generationTimestampNs{0U};
  std::uint64_t perceptionPublishTimestampUs{0U};

  std::uint32_t greenNorthSouthMs{0U};
  std::uint32_t greenEastWestMs{0U};
  std::uint32_t yellowMs{0U};
  std::uint32_t allRedMs{0U};
  std::uint32_t cycleLengthMs{0U};

  std::uint8_t emergencyNorthSouth{0U};
  std::uint8_t emergencyEastWest{0U};
  std::array<std::uint8_t, 2U> reserved{};
};

constexpr bool hasValidTimingPlanHeader(
    const TimingPlanMessageV1& message) noexcept {
  return message.magic == kTimingPlanMagic &&
         message.version == kTimingPlanVersion &&
         message.messageSize == kTimingPlanQueueMessageSize;
}

constexpr bool hasValidTimingPlanBooleanEncoding(
    const TimingPlanMessageV1& message) noexcept {
  return message.emergencyNorthSouth <= 1U &&
         message.emergencyEastWest <= 1U;
}

constexpr bool hasValidTimingPlanReservedBytes(
    const TimingPlanMessageV1& message) noexcept {
  return message.reserved[0U] == 0U && message.reserved[1U] == 0U;
}

constexpr bool HasValidTimingPlanEnvelope(
    const TimingPlanMessageV1& message) noexcept {
  return hasValidTimingPlanHeader(message) &&
         hasValidTimingPlanBooleanEncoding(message) &&
         hasValidTimingPlanReservedBytes(message) &&
         message.publisherInstanceId != 0U && message.sequenceNumber != 0U;
}

static_assert(sizeof(TimingPlanMessageV1) ==
                  static_cast<std::size_t>(kTimingPlanQueueMessageSize),
              "TimingPlanMessageV1 must remain exactly 72 bytes");
static_assert(std::is_standard_layout<TimingPlanMessageV1>::value,
              "TimingPlanMessageV1 must have a stable standard layout");
static_assert(std::is_trivially_copyable<TimingPlanMessageV1>::value,
              "TimingPlanMessageV1 must be transferable as POSIX MQ bytes");

}  // namespace traffic_ipc

#endif  // TRAFFIC_IPC_TIMING_PLAN_MESSAGE_V1_H
