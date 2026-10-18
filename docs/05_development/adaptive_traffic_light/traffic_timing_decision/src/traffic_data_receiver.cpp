#include "traffic_data_receiver.h"

#include <cmath>
#include <cstdint>
#include <string_view>

#include "application_logger.h"
#include "common.h"

namespace {

enum class SnapshotMicroseconds : std::uint64_t {
  kNanosecondsPerMicrosecond = 1000ULL,
  kMaximumFutureTolerance = 100000ULL,
  kMaximumAge = 6000000ULL,
};

constexpr std::uint64_t toMicroseconds(
    const SnapshotMicroseconds value) noexcept {
  return static_cast<std::uint64_t>(value);
}

enum class SnapshotCountLimit : std::uint32_t {
  kMaximumVehiclesPerDirection = 10000U,
};

constexpr std::uint32_t toCount(const SnapshotCountLimit value) noexcept {
  return static_cast<std::uint32_t>(value);
}

constexpr float kMinimumQueueLength = 0.0F;
constexpr float kMaximumQueueLength = 1000.0F;
constexpr float kMinimumOccupancy = 0.0F;
constexpr float kMaximumOccupancy = 1.0F;

bool validQueueLength(const float value) noexcept {
  return std::isfinite(value) && value >= kMinimumQueueLength &&
         value <= kMaximumQueueLength;
}
bool validOccupancy(const float value) noexcept {
  return std::isfinite(value) && value >= kMinimumOccupancy &&
         value <= kMaximumOccupancy;
}

}  // namespace

//
// SnapshotQueue
//
SnapshotQueue::SnapshotQueue()
    : queue_{traffic_ipc::kTrafficSnapshotQueueName,
             traffic_ipc::kTrafficSnapshotLockName} {}

bool SnapshotQueue::open() {
  lastStatus_ = queue_.open();
  return lastStatus_ == traffic_ipc::QueueStatus::kSuccess;
}

void SnapshotQueue::close() { queue_.close(); }

bool SnapshotQueue::receive(TrafficSnapshot& snapshot) {
  lastStatus_ = queue_.receiveLatest(snapshot);
  return lastStatus_ == traffic_ipc::QueueStatus::kSuccess;
}

traffic_ipc::QueueStatus SnapshotQueue::lastStatus() const noexcept {
  return lastStatus_;
}

std::int32_t SnapshotQueue::lastError() const noexcept {
  return static_cast<std::int32_t>(queue_.lastError());
}

//
// TrafficDataReceiver
//
TrafficDataReceiver::TrafficDataReceiver() = default;

bool TrafficDataReceiver::initialize() {
  if (!snapshotQueue.open()) {
    traffic_timing_decision::ipcLogger().LogError()
        << "[IPC][SNAPSHOT][OPEN] status="
        << std::string_view{traffic_ipc::queueStatusName(
               snapshotQueue.lastStatus())}
        << "; errno=" << snapshotQueue.lastError();
    return false;
  }
  traffic_timing_decision::ipcLogger().LogInfo()
      << "[IPC][SNAPSHOT][OPEN] queue="
      << traffic_ipc::kTrafficSnapshotQueueName
      << "; mode=nonblocking_consumer";
  return true;
}
void TrafficDataReceiver::shutdown() { snapshotQueue.close(); }

bool TrafficDataReceiver::requestSnapshot(
    TrafficSnapshot& snapshot, std::uint64_t& snapshotRxNs) {
  snapshotRxNs = std::uint64_t{};
  if (!snapshotQueue.receive(snapshot)) {
    return false;
  }
  // Capture the consumer-side boundary immediately after the latest snapshot
  // has been copied out of the IPC queue. Both downstream timestamps use
  // CLOCK_MONOTONIC, so their difference is not affected by wall-clock changes.
  snapshotRxNs = common::monotonicNanoseconds();
  return true;
}

bool TrafficDataReceiver::validateSnapshot(const TrafficSnapshot& snapshot) {
  const std::uint64_t nowUs =
      common::monotonicNanoseconds() /
      toMicroseconds(SnapshotMicroseconds::kNanosecondsPerMicrosecond);
  const bool identityValid = snapshot.frameId != std::uint64_t{} &&
                             snapshot.frameId > previousSnapshot.frameId;
  const bool timestampValid =
      snapshot.timestampUs != std::uint64_t{} &&
      snapshot.timestampUs <=
          nowUs +
              toMicroseconds(SnapshotMicroseconds::kMaximumFutureTolerance) &&
      nowUs <= snapshot.timestampUs +
                   toMicroseconds(SnapshotMicroseconds::kMaximumAge);
  const std::uint32_t maximumVehicleCount =
      toCount(SnapshotCountLimit::kMaximumVehiclesPerDirection);
  const bool countsValid = snapshot.vehicleCountNorth <= maximumVehicleCount &&
                           snapshot.vehicleCountSouth <= maximumVehicleCount &&
                           snapshot.vehicleCountEast <= maximumVehicleCount &&
                           snapshot.vehicleCountWest <= maximumVehicleCount;
  const bool queuesValid = validQueueLength(snapshot.queueLengthNorth) &&
                           validQueueLength(snapshot.queueLengthSouth) &&
                           validQueueLength(snapshot.queueLengthEast) &&
                           validQueueLength(snapshot.queueLengthWest);
  const bool occupancyValid = validOccupancy(snapshot.occupancyNorth) &&
                              validOccupancy(snapshot.occupancySouth) &&
                              validOccupancy(snapshot.occupancyEast) &&
                              validOccupancy(snapshot.occupancyWest);

  const bool valid = identityValid && timestampValid && countsValid &&
                     queuesValid && occupancyValid;
  if (valid) {
    latestSnapshot = snapshot;
    // previousSnapshot is the last accepted sequence watermark.
    previousSnapshot = snapshot;
  }
  return valid;
}

TrafficSnapshot TrafficDataReceiver::getLatestSnapshot() const {
  return latestSnapshot;
}

traffic_ipc::QueueStatus TrafficDataReceiver::lastQueueStatus() const noexcept {
  return snapshotQueue.lastStatus();
}

std::int32_t TrafficDataReceiver::lastQueueError() const noexcept {
  return snapshotQueue.lastError();
}
