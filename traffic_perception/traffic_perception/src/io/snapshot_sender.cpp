/**
 * @file   snapshot_sender.cpp
 * @brief  MQSnapshotSender implementation.
 *
 * Sends a TrafficSnapshot over a POSIX named message queue.
 * The snapshot is transmitted as raw bytes; a static_assert enforces that
 * TrafficSnapshot is trivially copyable before any byte-copy is attempted.
 *
 * Queue lifecycle:
 *   open()  – creates/opens the queue (O_CREAT | O_WRONLY, blocking mode).
 *   send()  – copies the snapshot bytes into mq_send().
 *   close() – releases the descriptor; does NOT unlink the queue.
 */

#include "traffic_perception/io/snapshot_sender.h"

#include <cerrno>    // errno
#include <cstring>   // std::strerror
#include <fcntl.h>   // O_CREAT, O_WRONLY
#include <iostream>  // std::cerr, std::cout
#include <sys/stat.h>

namespace traffic_perception {

// Guarantee at compile time that a raw byte-copy of TrafficSnapshot is valid.
static_assert(std::is_trivially_copyable_v<TrafficSnapshot>,
              "TrafficSnapshot must be trivially copyable for POSIX MQ transport");

// The queue name used by the traffic perception module.
static const char kQueueName[] = "/traffic_snapshot_q";

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

MQSnapshotSender::MQSnapshotSender()
    : mqDescriptor{static_cast<mqd_t>(-1)},
      queueName{kQueueName},
      attributes{} {}

MQSnapshotSender::~MQSnapshotSender() {
  // Ensure the descriptor is released even if the caller forgot to call close().
  close();
}

// ---------------------------------------------------------------------------
// ISnapshotSender interface
// ---------------------------------------------------------------------------

/**
 * @brief Open (or create) the POSIX message queue.
 *
 * If open() is called a second time the previously opened descriptor is
 * released first to avoid a resource leak.
 *
 * @return true on success, false on failure (error printed to stderr).
 */
bool MQSnapshotSender::open() {
  // Close any existing descriptor to prevent leaks.
  if (mqDescriptor != static_cast<mqd_t>(-1)) {
    ::mq_close(mqDescriptor);
    mqDescriptor = static_cast<mqd_t>(-1);
  }

  // One message slot is enough; the consumer is expected to drain quickly.
  attributes.mq_flags   = 0;
  attributes.mq_maxmsg  = 10;
  attributes.mq_msgsize = static_cast<long>(sizeof(TrafficSnapshot));
  attributes.mq_curmsgs = 0;

  mqDescriptor = ::mq_open(queueName,
                           O_CREAT | O_WRONLY | O_NONBLOCK,
                           0644,        // rw-r--r-- permissions
                           &attributes);
  if (mqDescriptor == static_cast<mqd_t>(-1)) {
    std::cerr << "[MQSnapshotSender][ERROR] mq_open(\"" << queueName
              << "\") failed: " << std::strerror(errno) << '\n';
    return false;
  }

  std::cout << "[MQSnapshotSender] queue opened: " << queueName << '\n';
  return true;
}

/**
 * @brief Transmit a TrafficSnapshot via the POSIX message queue.
 *
 * @param snapshot The snapshot to send. Sent as a raw binary message.
 * @return true on success, false on failure (error printed to stderr).
 */
bool MQSnapshotSender::send(const TrafficSnapshot& snapshot) {
  if (mqDescriptor == static_cast<mqd_t>(-1)) {
    std::cerr << "[MQSnapshotSender][ERROR] queue not open – call open() first\n";
    return false;
  }

  const char* msgPtr = reinterpret_cast<const char*>(&snapshot);

  if (::mq_send(mqDescriptor, msgPtr, sizeof(TrafficSnapshot), 0) == 0) {
    return true;
  }

  if (errno == EAGAIN) {
    std::cout << "[MQSnapshotSender] Queue full, dropping oldest snapshot." << std::endl;
    
    char buffer[sizeof(TrafficSnapshot)];
    if (::mq_receive(mqDescriptor, buffer, sizeof(TrafficSnapshot), nullptr) != -1) {
      if (::mq_send(mqDescriptor, msgPtr, sizeof(TrafficSnapshot), 0) == 0) {
        return true;
      }
      std::cerr << "[MQSnapshotSender] Retry send failed: " << std::strerror(errno) << '\n';
    }
  }

  std::cerr << "[MQSnapshotSender][ERROR] mq_send failed: "
            << std::strerror(errno) << '\n';
  return false;
}

/**
 * @brief Close the POSIX message queue descriptor.
 *
 * Safe to call multiple times. Does NOT unlink the queue from the system
 * so that other processes can continue to consume remaining messages.
 */
void MQSnapshotSender::close() {
  if (mqDescriptor != static_cast<mqd_t>(-1)) {
    ::mq_close(mqDescriptor);
    mqDescriptor = static_cast<mqd_t>(-1);
    std::cout << "[MQSnapshotSender] queue closed: " << queueName << '\n';
  }
}

}  // namespace traffic_perception
