#ifndef TRAFFIC_IPC_LATEST_VALUE_QUEUE_H
#define TRAFFIC_IPC_LATEST_VALUE_QUEUE_H

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <mqueue.h>
#include <optional>
#include <semaphore.h>
#include <sys/stat.h>
#include <type_traits>

namespace traffic_ipc {

inline constexpr long kQueueDepth = 4L;
inline constexpr unsigned int kMessagePriority = 0U;

enum class QueueStatus {
  kSuccess,
  kBusy,
  kEmpty,
  kNotFound,
  kDeferred,
  kContractMismatch,
  kSystemError,
};

inline const char* queueStatusName(const QueueStatus status) noexcept {
  switch (status) {
    case QueueStatus::kSuccess:
      return "success";
    case QueueStatus::kBusy:
      return "busy";
    case QueueStatus::kEmpty:
      return "empty";
    case QueueStatus::kNotFound:
      return "not_found";
    case QueueStatus::kDeferred:
      return "deferred";
    case QueueStatus::kContractMismatch:
      return "contract_mismatch";
    case QueueStatus::kSystemError:
      return "system_error";
  }
  return "unknown";
}

namespace detail {

class SemaphoreGuard {
 public:
  explicit SemaphoreGuard(sem_t* semaphore) noexcept : semaphore_{semaphore} {}
  ~SemaphoreGuard() {
    if (semaphore_ != SEM_FAILED) {
      (void)sem_post(semaphore_);
    }
  }

  SemaphoreGuard(const SemaphoreGuard&) = delete;
  SemaphoreGuard& operator=(const SemaphoreGuard&) = delete;

 private:
  sem_t* semaphore_;
};

inline QueueStatus tryLock(sem_t* semaphore, int& lastError) noexcept {
  if (semaphore == SEM_FAILED) {
    lastError = EBADF;
    return QueueStatus::kSystemError;
  }

  if (sem_trywait(semaphore) == 0) {
    lastError = 0;
    return QueueStatus::kSuccess;
  }

  lastError = errno;
  if (errno == EAGAIN || errno == EINTR) {
    return QueueStatus::kBusy;
  }
  return QueueStatus::kSystemError;
}

template <typename Message>
QueueStatus validateQueueContract(const mqd_t descriptor,
                                  int& lastError) noexcept {
  mq_attr attributes{};
  if (mq_getattr(descriptor, &attributes) != 0) {
    lastError = errno;
    return QueueStatus::kSystemError;
  }
  if (attributes.mq_msgsize != static_cast<long>(sizeof(Message)) ||
      attributes.mq_maxmsg != kQueueDepth) {
    lastError = EMSGSIZE;
    return QueueStatus::kContractMismatch;
  }
  lastError = 0;
  return QueueStatus::kSuccess;
}

}  // namespace detail

// Publisher owns creation of the queue. Publishing never blocks. If another
// process currently owns the shared IPC lock, only the newest pending value is
// retained locally and retried on the publisher's next periodic cycle.
template <typename Message>
class LatestValuePublisher {
  static_assert(std::is_trivially_copyable<Message>::value,
                "POSIX MQ messages must be trivially copyable");

 public:
  LatestValuePublisher(const char* queueName, const char* lockName) noexcept
      : queueName_{queueName}, lockName_{lockName} {}

  ~LatestValuePublisher() { close(); }

  LatestValuePublisher(const LatestValuePublisher&) = delete;
  LatestValuePublisher& operator=(const LatestValuePublisher&) = delete;

  QueueStatus open(const bool resetOwnedObjects = false) noexcept {
    if (resetOwnedObjects) {
      close();
      pendingLatest_.reset();
      if (mq_unlink(queueName_) != 0 && errno != ENOENT) {
        lastError_ = errno;
        return QueueStatus::kSystemError;
      }
      if (sem_unlink(lockName_) != 0 && errno != ENOENT) {
        lastError_ = errno;
        return QueueStatus::kSystemError;
      }
    }
    if (descriptor_ != static_cast<mqd_t>(-1) && semaphore_ != SEM_FAILED) {
      return QueueStatus::kSuccess;
    }

    semaphore_ = sem_open(lockName_, O_CREAT, static_cast<mode_t>(0660), 1U);
    if (semaphore_ == SEM_FAILED) {
      lastError_ = errno;
      return QueueStatus::kSystemError;
    }

    mq_attr attributes{};
    attributes.mq_flags = 0L;
    attributes.mq_maxmsg = kQueueDepth;
    attributes.mq_msgsize = static_cast<long>(sizeof(Message));
    attributes.mq_curmsgs = 0L;

    descriptor_ = mq_open(queueName_, O_CREAT | O_RDWR | O_NONBLOCK | O_CLOEXEC,
                          static_cast<mode_t>(0660), &attributes);
    if (descriptor_ == static_cast<mqd_t>(-1)) {
      lastError_ = errno;
      (void)sem_close(semaphore_);
      semaphore_ = SEM_FAILED;
      return QueueStatus::kSystemError;
    }

    const auto status =
        detail::validateQueueContract<Message>(descriptor_, lastError_);
    if (status != QueueStatus::kSuccess) {
      close();
    }
    return status;
  }

  void close() noexcept {
    if (descriptor_ != static_cast<mqd_t>(-1)) {
      (void)mq_close(descriptor_);
      descriptor_ = static_cast<mqd_t>(-1);
    }
    if (semaphore_ != SEM_FAILED) {
      (void)sem_close(semaphore_);
      semaphore_ = SEM_FAILED;
    }
  }

  QueueStatus publish(const Message& newest) noexcept {
    if (descriptor_ == static_cast<mqd_t>(-1) || semaphore_ == SEM_FAILED) {
      lastError_ = EBADF;
      pendingLatest_ = newest;
      return QueueStatus::kSystemError;
    }

    // The caller supplies values in chronological order. The new argument is
    // therefore always at least as recent as the single deferred local slot.
    pendingLatest_ = newest;
    const auto lockStatus = detail::tryLock(semaphore_, lastError_);
    if (lockStatus == QueueStatus::kBusy) {
      return QueueStatus::kDeferred;
    }
    if (lockStatus != QueueStatus::kSuccess) {
      return lockStatus;
    }
    detail::SemaphoreGuard unlock{semaphore_};

    const Message candidate = *pendingLatest_;
    if (mq_send(descriptor_, reinterpret_cast<const char*>(&candidate),
                sizeof(candidate), kMessagePriority) == 0) {
      pendingLatest_.reset();
      lastError_ = 0;
      return QueueStatus::kSuccess;
    }

    if (errno != EAGAIN) {
      lastError_ = errno;
      return QueueStatus::kSystemError;
    }

    // The queue is full. The shared semaphore makes this receive->send pair
    // logically atomic with respect to the consumer drain operation.
    Message discarded{};
    if (mq_receive(descriptor_, reinterpret_cast<char*>(&discarded),
                   sizeof(discarded), nullptr) < 0) {
      lastError_ = errno;
      return QueueStatus::kSystemError;
    }

    if (mq_send(descriptor_, reinterpret_cast<const char*>(&candidate),
                sizeof(candidate), kMessagePriority) != 0) {
      lastError_ = errno;
      return QueueStatus::kSystemError;
    }

    pendingLatest_.reset();
    lastError_ = 0;
    return QueueStatus::kSuccess;
  }

  int lastError() const noexcept { return lastError_; }

 private:
  const char* queueName_;
  const char* lockName_;
  mqd_t descriptor_{static_cast<mqd_t>(-1)};
  sem_t* semaphore_{SEM_FAILED};
  std::optional<Message> pendingLatest_{};
  int lastError_{0};
};

// Consumer opens but never creates the channel. It tries the inter-process
// lock once, drains the bounded queue, and returns the latest queued value.
template <typename Message>
class LatestValueConsumer {
  static_assert(std::is_trivially_copyable<Message>::value,
                "POSIX MQ messages must be trivially copyable");

 public:
  LatestValueConsumer(const char* queueName, const char* lockName) noexcept
      : queueName_{queueName}, lockName_{lockName} {}

  ~LatestValueConsumer() { close(); }

  LatestValueConsumer(const LatestValueConsumer&) = delete;
  LatestValueConsumer& operator=(const LatestValueConsumer&) = delete;

  QueueStatus open() noexcept {
    if (descriptor_ != static_cast<mqd_t>(-1) && semaphore_ != SEM_FAILED) {
      return QueueStatus::kSuccess;
    }

    semaphore_ = sem_open(lockName_, 0);
    if (semaphore_ == SEM_FAILED) {
      lastError_ = errno;
      return errno == ENOENT ? QueueStatus::kNotFound
                             : QueueStatus::kSystemError;
    }

    descriptor_ = mq_open(queueName_, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (descriptor_ == static_cast<mqd_t>(-1)) {
      const int openError = errno;
      lastError_ = openError;
      (void)sem_close(semaphore_);
      semaphore_ = SEM_FAILED;
      return openError == ENOENT ? QueueStatus::kNotFound
                                 : QueueStatus::kSystemError;
    }

    const auto status =
        detail::validateQueueContract<Message>(descriptor_, lastError_);
    if (status != QueueStatus::kSuccess) {
      close();
    }
    return status;
  }

  void close() noexcept {
    if (descriptor_ != static_cast<mqd_t>(-1)) {
      (void)mq_close(descriptor_);
      descriptor_ = static_cast<mqd_t>(-1);
    }
    if (semaphore_ != SEM_FAILED) {
      (void)sem_close(semaphore_);
      semaphore_ = SEM_FAILED;
    }
  }

  bool isOpen() const noexcept {
    return descriptor_ != static_cast<mqd_t>(-1) && semaphore_ != SEM_FAILED;
  }

  QueueStatus receiveLatest(Message& latest) noexcept {
    if (!isOpen()) {
      lastError_ = EBADF;
      return QueueStatus::kNotFound;
    }

    const auto lockStatus = detail::tryLock(semaphore_, lastError_);
    if (lockStatus != QueueStatus::kSuccess) {
      return lockStatus;
    }
    detail::SemaphoreGuard unlock{semaphore_};

    Message candidate{};
    bool received{false};
    for (long index = 0L; index < kQueueDepth; ++index) {
      Message current{};
      const auto bytes = mq_receive(descriptor_,
                                    reinterpret_cast<char*>(&current),
                                    sizeof(current), nullptr);
      if (bytes == static_cast<ssize_t>(sizeof(current))) {
        candidate = current;
        received = true;
        continue;
      }
      if (bytes < 0 && errno == EAGAIN) {
        break;
      }
      lastError_ = bytes < 0 ? errno : EMSGSIZE;
      return bytes < 0 ? QueueStatus::kSystemError
                       : QueueStatus::kContractMismatch;
    }

    if (!received) {
      lastError_ = EAGAIN;
      return QueueStatus::kEmpty;
    }
    latest = candidate;
    lastError_ = 0;
    return QueueStatus::kSuccess;
  }

  int lastError() const noexcept { return lastError_; }

 private:
  const char* queueName_;
  const char* lockName_;
  mqd_t descriptor_{static_cast<mqd_t>(-1)};
  sem_t* semaphore_{SEM_FAILED};
  int lastError_{0};
};

}  // namespace traffic_ipc

#endif  // TRAFFIC_IPC_LATEST_VALUE_QUEUE_H
