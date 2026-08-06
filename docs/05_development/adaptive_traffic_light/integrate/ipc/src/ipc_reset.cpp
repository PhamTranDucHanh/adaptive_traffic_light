#include <cerrno>
#include <mqueue.h>
#include <semaphore.h>

#include "traffic_ipc/messages.h"
#include "traffic_ipc/signal_state_message_v1.h"
#include "traffic_ipc/timing_plan_message_v1.h"

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
  const bool signalStateQueueRemoved =
      unlinkQueue(traffic_ipc::kSignalStateQueueName);
  const bool snapshotLockRemoved =
      unlinkSemaphore(traffic_ipc::kTrafficSnapshotLockName);
  const bool signalStateLockRemoved =
      unlinkSemaphore(traffic_ipc::kSignalStateLockName);

  return snapshotQueueRemoved && timingPlanQueueRemoved &&
                 signalStateQueueRemoved && snapshotLockRemoved &&
                 signalStateLockRemoved
             ? 0
             : 1;
}
