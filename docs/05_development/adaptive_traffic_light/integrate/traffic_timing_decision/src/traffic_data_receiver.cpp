#include "traffic_data_receiver.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>
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
  if (lastStatus_ != traffic_ipc::QueueStatus::kSuccess) {
    lastError_ = queue_.lastError();
    return false;
  }

  // This second descriptor is used only as a Linux poll() readiness source.
  // Actual receives still go through LatestValueConsumer so the existing
  // non-blocking semaphore-protected drain-latest contract remains unchanged.
  readinessDescriptor_ = mq_open(traffic_ipc::kTrafficSnapshotQueueName,
                                 O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (readinessDescriptor_ == static_cast<mqd_t>(-1)) {
    lastError_ = errno;
    lastStatus_ = traffic_ipc::QueueStatus::kSystemError;
    queue_.close();
    return false;
  }

  stopEventDescriptor_ = eventfd(0U, EFD_NONBLOCK | EFD_CLOEXEC);
  if (stopEventDescriptor_ < 0) {
    lastError_ = errno;
    lastStatus_ = traffic_ipc::QueueStatus::kSystemError;
    (void)mq_close(readinessDescriptor_);
    readinessDescriptor_ = static_cast<mqd_t>(-1);
    queue_.close();
    return false;
  }

  lastError_ = 0;
  return true;
}

void SnapshotQueue::close() {
  if (stopEventDescriptor_ >= 0) {
    (void)::close(stopEventDescriptor_);
    stopEventDescriptor_ = -1;
  }
  if (readinessDescriptor_ != static_cast<mqd_t>(-1)) {
    (void)mq_close(readinessDescriptor_);
    readinessDescriptor_ = static_cast<mqd_t>(-1);
  }
  queue_.close();
}

SnapshotWaitStatus SnapshotQueue::waitAndReceive(
    TrafficSnapshot& snapshot, const std::uint32_t timeoutMs) noexcept {
  if (readinessDescriptor_ == static_cast<mqd_t>(-1) ||
      stopEventDescriptor_ < 0) {
    lastError_ = EBADF;
    lastStatus_ = traffic_ipc::QueueStatus::kNotFound;
    return SnapshotWaitStatus::kError;
  }

  pollfd descriptors[2]{};
  descriptors[0].fd = static_cast<std::int32_t>(readinessDescriptor_);
  descriptors[0].events = POLLIN;
  descriptors[1].fd = stopEventDescriptor_;
  descriptors[1].events = POLLIN;

  const std::int32_t waitResult =
      poll(descriptors, 2U, static_cast<std::int32_t>(timeoutMs));
  if (waitResult == 0) {
    lastError_ = EAGAIN;
    lastStatus_ = traffic_ipc::QueueStatus::kEmpty;
    return SnapshotWaitStatus::kTimeout;
  }
  if (waitResult < 0) {
    lastError_ = errno;
    if (lastError_ == EINTR) {
      lastStatus_ = traffic_ipc::QueueStatus::kBusy;
      return SnapshotWaitStatus::kRetry;
    }
    lastStatus_ = traffic_ipc::QueueStatus::kSystemError;
    return SnapshotWaitStatus::kError;
  }

  if ((descriptors[1].revents & POLLIN) != 0) {
    eventfd_t stopValue{};
    (void)eventfd_read(stopEventDescriptor_, &stopValue);
    lastError_ = ECANCELED;
    lastStatus_ = traffic_ipc::QueueStatus::kDeferred;
    return SnapshotWaitStatus::kStopped;
  }

  if ((descriptors[0].revents & POLLIN) != 0) {
    lastStatus_ = queue_.receiveLatest(snapshot);
    lastError_ = queue_.lastError();
    if (lastStatus_ == traffic_ipc::QueueStatus::kSuccess) {
      return SnapshotWaitStatus::kReady;
    }
    if (lastStatus_ == traffic_ipc::QueueStatus::kBusy ||
        lastStatus_ == traffic_ipc::QueueStatus::kEmpty) {
      return SnapshotWaitStatus::kRetry;
    }
    return SnapshotWaitStatus::kError;
  }

  lastError_ = EIO;
  lastStatus_ = traffic_ipc::QueueStatus::kSystemError;
  return SnapshotWaitStatus::kError;
}

void SnapshotQueue::requestStop() noexcept {
  if (stopEventDescriptor_ < 0) {
    return;
  }
  const eventfd_t stopValue{1U};
  if (eventfd_write(stopEventDescriptor_, stopValue) != 0 && errno != EAGAIN) {
    lastError_ = errno;
  }
}

traffic_ipc::QueueStatus SnapshotQueue::lastStatus() const noexcept {
  return lastStatus_;
}

std::int32_t SnapshotQueue::lastError() const noexcept { return lastError_; }

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
      << "; mode=event_driven_poll_then_nonblocking_drain_latest";
  return true;
}
void TrafficDataReceiver::shutdown() { snapshotQueue.close(); }

SnapshotWaitStatus TrafficDataReceiver::waitForSnapshot(
    TrafficSnapshot& snapshot, const std::uint32_t timeoutMs) noexcept {
  return snapshotQueue.waitAndReceive(snapshot, timeoutMs);
}

void TrafficDataReceiver::requestStop() noexcept {
  snapshotQueue.requestStop();
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
