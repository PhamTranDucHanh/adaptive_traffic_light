#ifndef TRAFFIC_IPC_TIMING_PLAN_MESSAGE_V1_H_
#define TRAFFIC_IPC_TIMING_PLAN_MESSAGE_V1_H_

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace traffic_ipc {

inline constexpr char kTimingPlanQueueName[] = "/traffic_timing_plan_v1";
inline constexpr long kTimingPlanQueueMaxMessages{8L};
inline constexpr long kTimingPlanQueueMessageSize{64L};
inline constexpr std::uint32_t kTimingPlanMessageMagic{0x54504C31U};
inline constexpr std::uint16_t kTimingPlanMessageVersion{1U};

struct TimingPlanMessageV1 {
  std::uint32_t magic{kTimingPlanMessageMagic};
  std::uint16_t version{kTimingPlanMessageVersion};
  std::uint16_t messageSize{sizeof(TimingPlanMessageV1)};

  std::uint64_t publisherInstanceId{0U};
  std::uint64_t sequenceNumber{0U};

  std::uint64_t planId{0U};
  std::uint64_t generationTimestampNs{0U};

  std::uint32_t greenNorthSouthMs{0U};
  std::uint32_t greenEastWestMs{0U};
  std::uint32_t yellowMs{3000U};
  std::uint32_t allRedMs{1000U};
  std::uint32_t cycleLengthMs{0U};

  std::uint8_t emergencyNorthSouth{0U};
  std::uint8_t emergencyEastWest{0U};
  std::uint8_t reserved[2]{0U, 0U};
};

inline bool HasValidTimingPlanEnvelope(
    const TimingPlanMessageV1& message) noexcept {
  return message.magic == kTimingPlanMessageMagic &&
         message.version == kTimingPlanMessageVersion &&
         message.messageSize == sizeof(TimingPlanMessageV1) &&
         message.publisherInstanceId != 0U && message.sequenceNumber != 0U &&
         message.emergencyNorthSouth <= 1U &&
         message.emergencyEastWest <= 1U;
}

static_assert(sizeof(TimingPlanMessageV1) == 64U,
              "TimingPlanMessageV1 must match mq_msgsize=64");
static_assert(offsetof(TimingPlanMessageV1, publisherInstanceId) == 8U,
              "Unexpected TimingPlanMessageV1 publisher offset");
static_assert(offsetof(TimingPlanMessageV1, planId) == 24U,
              "Unexpected TimingPlanMessageV1 plan offset");
static_assert(offsetof(TimingPlanMessageV1, emergencyNorthSouth) == 60U,
              "Unexpected TimingPlanMessageV1 boolean offset");
static_assert(std::is_standard_layout<TimingPlanMessageV1>::value,
              "TimingPlanMessageV1 must have standard layout");
static_assert(std::is_trivially_copyable<TimingPlanMessageV1>::value,
              "TimingPlanMessageV1 must be transferable as POSIX MQ bytes");

}  // namespace traffic_ipc

#endif  // TRAFFIC_IPC_TIMING_PLAN_MESSAGE_V1_H_
