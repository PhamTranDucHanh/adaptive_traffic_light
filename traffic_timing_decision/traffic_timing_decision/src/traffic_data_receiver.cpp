#include "traffic_data_receiver.h"

#include <cmath>
#include <cstdint>
#include <string_view>

#include "application_logger.h"
#include "common/absolute_periodic.h"

namespace {

constexpr std::uint64_t kMaximumSnapshotAgeUs = 6000000ULL;
constexpr std::uint64_t kMaximumFutureToleranceUs = 100000ULL;
constexpr std::uint32_t kMaximumVehicleCount = 10000U;
constexpr float kMaximumQueueLength = 1000.0F;

bool validQueueLength(const float value) noexcept {
  return std::isfinite(value) && value >= 0.0F &&
         value <= kMaximumQueueLength;
}

bool validOccupancy(const float value) noexcept {
  return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
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

int SnapshotQueue::lastError() const noexcept { return queue_.lastError(); }

//
// TrafficDataReceiver
//
TrafficDataReceiver::TrafficDataReceiver() = default;

bool TrafficDataReceiver::initialize() {
  if (!snapshotQueue.open()) {
    traffic_timing_decision::applicationLogger().LogError()
        << "[IPC][SNAPSHOT][OPEN] status="
        << std::string_view{
               traffic_ipc::queueStatusName(snapshotQueue.lastStatus())}
        << "; errno=" << snapshotQueue.lastError();
    return false;
  }
  traffic_timing_decision::applicationLogger().LogInfo()
      << "[IPC][SNAPSHOT][OPEN] queue="
      << traffic_ipc::kTrafficSnapshotQueueName << "; mode=nonblocking_consumer";
  return true;
}
void TrafficDataReceiver::shutdown() { snapshotQueue.close(); }

bool TrafficDataReceiver::requestSnapshot(TrafficSnapshot& snapshot) {
  if (!snapshotQueue.receive(snapshot)) {
    return false;
  }
  return true;
}

bool TrafficDataReceiver::validateSnapshot(const TrafficSnapshot& snapshot) {
  const std::uint64_t nowUs =
      common::monotonicNanoseconds() / 1000ULL;
  const bool identityValid =
      snapshot.frameId != 0U && snapshot.frameId > previousSnapshot.frameId;
  const bool timestampValid =
      snapshot.timestampUs != 0U &&
      snapshot.timestampUs <= nowUs + kMaximumFutureToleranceUs &&
      nowUs <= snapshot.timestampUs + kMaximumSnapshotAgeUs;
  const bool countsValid =
      snapshot.vehicleCountNorth <= kMaximumVehicleCount &&
      snapshot.vehicleCountSouth <= kMaximumVehicleCount &&
      snapshot.vehicleCountEast <= kMaximumVehicleCount &&
      snapshot.vehicleCountWest <= kMaximumVehicleCount;
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

int TrafficDataReceiver::lastQueueError() const noexcept {
  return snapshotQueue.lastError();
}
