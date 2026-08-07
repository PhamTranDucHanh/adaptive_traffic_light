#ifndef TRAFFIC_IPC_SIGNAL_STATE_MESSAGE_V1_H
#define TRAFFIC_IPC_SIGNAL_STATE_MESSAGE_V1_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace traffic_ipc {

inline constexpr char kSignalStateQueueName[] = "/traffic_signal_state_v1";
inline constexpr char kSignalStateLockName[] =
    "/traffic_signal_state_lock_v1";
inline constexpr std::uint32_t kSignalStateMagic = 0x53494731U;  // "SIG1"
inline constexpr std::uint16_t kSignalStateVersion = 1U;
inline constexpr std::uint16_t kSignalStateMessageSize = 40U;

enum class SignalLamp : std::uint8_t {
  kRed = 0U,
  kYellow = 1U,
  kGreen = 2U,
};

enum class SignalPhase : std::uint8_t {
  kNorthSouthGreen = 0U,
  kYellow = 1U,
  kAllRed = 2U,
  kEastWestGreen = 3U,
};

// Latest-value display contract from Signal Controller to Perception. This is
// visualization-only telemetry and must never feed back into control logic.
struct SignalStateMessageV1 final {
  std::uint32_t magic{kSignalStateMagic};
  std::uint16_t version{kSignalStateVersion};
  std::uint16_t messageSize{kSignalStateMessageSize};

  std::uint64_t sequenceNumber{0U};
  std::uint64_t publishTimestampNs{0U};
  std::uint32_t northSouthRemainingTimeMs{0U};
  std::uint32_t eastWestRemainingTimeMs{0U};

  SignalLamp northSouthLamp{SignalLamp::kRed};
  SignalLamp eastWestLamp{SignalLamp::kRed};
  SignalPhase phase{SignalPhase::kAllRed};
  std::array<std::uint8_t, 5U> reserved{};
};

constexpr bool HasValidSignalStateEnvelope(
    const SignalStateMessageV1& message) noexcept {
  return message.magic == kSignalStateMagic &&
         message.version == kSignalStateVersion &&
         message.messageSize == kSignalStateMessageSize &&
         message.sequenceNumber != 0U && message.publishTimestampNs != 0U &&
         message.northSouthLamp <= SignalLamp::kGreen &&
         message.eastWestLamp <= SignalLamp::kGreen &&
         message.phase <= SignalPhase::kEastWestGreen &&
         message.reserved[0U] == 0U && message.reserved[1U] == 0U &&
         message.reserved[2U] == 0U && message.reserved[3U] == 0U &&
         message.reserved[4U] == 0U;
}

static_assert(sizeof(SignalStateMessageV1) == kSignalStateMessageSize,
              "SignalStateMessageV1 must remain exactly 40 bytes");
static_assert(std::is_standard_layout<SignalStateMessageV1>::value,
              "SignalStateMessageV1 must have a stable standard layout");
static_assert(std::is_trivially_copyable<SignalStateMessageV1>::value,
              "SignalStateMessageV1 must be transferable as POSIX MQ bytes");

}  // namespace traffic_ipc

#endif  // TRAFFIC_IPC_SIGNAL_STATE_MESSAGE_V1_H
