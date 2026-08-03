#include "traffic_perception/io/snapshot_sender.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string_view>

#include "score/mw/log/logging.h"

namespace {

std::uint32_t ToWireCount(const std::int32_t count) noexcept {
  return count > 0 ? static_cast<std::uint32_t>(count) : 0U;
}

traffic_ipc::TrafficSnapshot ToWireSnapshot(
    const TrafficSnapshot& source) noexcept {
  traffic_ipc::TrafficSnapshot wire{};
  wire.frameId = source.frameId > 0 ? static_cast<std::uint64_t>(source.frameId)
                                    : 0U;
  wire.timestampUs = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());

  wire.vehicleCountNorth = ToWireCount(source.vehicleCountNorth);
  wire.vehicleCountSouth = ToWireCount(source.vehicleCountSouth);
  wire.vehicleCountEast = ToWireCount(source.vehicleCountEast);
  wire.vehicleCountWest = ToWireCount(source.vehicleCountWest);

  wire.queueLengthNorth = source.queueLengthNorth;
  wire.queueLengthSouth = source.queueLengthSouth;
  wire.queueLengthEast = source.queueLengthEast;
  wire.queueLengthWest = source.queueLengthWest;
  wire.occupancyNorth = source.occupancyNorth;
  wire.occupancySouth = source.occupancySouth;
  wire.occupancyEast = source.occupancyEast;
  wire.occupancyWest = source.occupancyWest;
  wire.emergencyNorth = source.emergencyNorth;
  wire.emergencySouth = source.emergencySouth;
  wire.emergencyEast = source.emergencyEast;
  wire.emergencyWest = source.emergencyWest;
  return wire;
}

}  // namespace

namespace traffic_perception {

MQSnapshotSender::MQSnapshotSender() = default;

MQSnapshotSender::~MQSnapshotSender() { close(); }

/**
 * @brief Open (or create) the POSIX message queue.
 *
 * If open() is called a second time the previously opened descriptor is
 * released first to avoid a resource leak.
 *
 * @return true on success, false on failure (error printed to stderr).
 */
bool MQSnapshotSender::open() {
  if (open_) {
    return true;
  }

  const auto status = publisher_.open();
  if (status != traffic_ipc::QueueStatus::kSuccess) {
    score::mw::log::LogError()
        << "[IPC][SNAPSHOT][OPEN] queue="
        << traffic_ipc::kTrafficSnapshotQueueName
        << "; status=" << std::string_view{traffic_ipc::queueStatusName(status)}
        << "; errno=" << publisher_.lastError();
    return false;
  }

  open_ = true;
  score::mw::log::LogInfo()
      << "[IPC][SNAPSHOT][OPEN] queue="
      << traffic_ipc::kTrafficSnapshotQueueName
      << "; mode=nonblocking_latest_value_producer"
      << "; maxmsg=" << traffic_ipc::kQueueDepth
      << "; msgsize=" << sizeof(traffic_ipc::TrafficSnapshot);
  return true;
}

/**
 * @brief Transmit a TrafficSnapshot via the POSIX message queue.
 *
 * @param snapshot The snapshot to send. Sent as a raw binary message.
 * @return true on success, false on failure (error printed to stderr).
 */
bool MQSnapshotSender::send(const TrafficSnapshot& snapshot) {
  if (!open_) {
    score::mw::log::LogError()
        << "[IPC][SNAPSHOT][SEND] status=not_open";
    return false;
  }

  const traffic_ipc::TrafficSnapshot wire = ToWireSnapshot(snapshot);
  const auto status = publisher_.publish(wire);
  if (status == traffic_ipc::QueueStatus::kSuccess) {
    score::mw::log::LogInfo()
        << "[IPC][SNAPSHOT][SENT] frame_id=" << wire.frameId
        << "; timestamp_us=" << wire.timestampUs;
    return true;
  }

  if (status == traffic_ipc::QueueStatus::kDeferred) {
    score::mw::log::LogDebug()
        << "[IPC][SNAPSHOT][DEFERRED] frame_id=" << wire.frameId
        << "; reason=ipc_lock_busy; retry=next_snapshot";
    return true;
  }

  score::mw::log::LogError()
      << "[IPC][SNAPSHOT][SEND] frame_id=" << wire.frameId
      << "; status=" << std::string_view{traffic_ipc::queueStatusName(status)}
      << "; errno=" << publisher_.lastError();
  return false;
}

/**
 * @brief Close the POSIX message queue descriptor.
 *
 * Safe to call multiple times. Does NOT unlink the queue from the system
 * so that other processes can continue to consume remaining messages.
 */
void MQSnapshotSender::close() {
  if (open_) {
    publisher_.close();
    open_ = false;
    score::mw::log::LogInfo()
        << "[IPC][SNAPSHOT][CLOSED] queue="
        << traffic_ipc::kTrafficSnapshotQueueName;
  }
}

}  // namespace traffic_perception
