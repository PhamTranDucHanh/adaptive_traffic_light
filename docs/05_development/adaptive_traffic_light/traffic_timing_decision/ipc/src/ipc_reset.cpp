#include <cerrno>
#include <mqueue.h>
#include <semaphore.h>

#include "traffic_ipc/messages.h"

namespace {

bool unlinkQueue(const char* name) noexcept {
  return mq_unlink(name) == 0 || errno == ENOENT;
}

bool unlinkSemaphore(const char* name) noexcept {
  return sem_unlink(name) == 0 || errno == ENOENT;
}

}  // namespace

int main() {
  const bool snapshotQueueRemoved =
      unlinkQueue(traffic_ipc::kTrafficSnapshotQueueName);
  const bool timingPlanQueueRemoved =
      unlinkQueue(traffic_ipc::kTimingPlanQueueName);
  const bool snapshotLockRemoved =
      unlinkSemaphore(traffic_ipc::kTrafficSnapshotLockName);
  const bool timingPlanLockRemoved =
      unlinkSemaphore(traffic_ipc::kTimingPlanLockName);

  return snapshotQueueRemoved && timingPlanQueueRemoved &&
                 snapshotLockRemoved && timingPlanLockRemoved
             ? 0
             : 1;
}
