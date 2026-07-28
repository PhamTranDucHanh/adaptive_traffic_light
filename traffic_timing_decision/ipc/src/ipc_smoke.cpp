#include <cerrno>
#include <mqueue.h>
#include <semaphore.h>

#include "traffic_ipc/latest_value_queue.h"
#include "traffic_ipc/messages.h"

namespace {

constexpr char kTestQueue[] = "/traffic_ipc_smoke_queue";
constexpr char kTestLock[] = "/traffic_ipc_smoke_lock";

void cleanup() noexcept {
  (void)mq_unlink(kTestQueue);
  (void)sem_unlink(kTestLock);
}

}  // namespace

int main() {
  cleanup();
  traffic_ipc::LatestValuePublisher<traffic_ipc::TrafficSnapshot> publisher{
      kTestQueue, kTestLock};
  if (publisher.open() != traffic_ipc::QueueStatus::kSuccess) {
    cleanup();
    return 1;
  }

  // Six sends into a four-slot queue exercise the drop-oldest replacement.
  for (std::uint64_t frame = 1U; frame <= 6U; ++frame) {
    traffic_ipc::TrafficSnapshot snapshot{};
    snapshot.frameId = frame;
    if (publisher.publish(snapshot) != traffic_ipc::QueueStatus::kSuccess) {
      cleanup();
      return 2;
    }
  }

  traffic_ipc::LatestValueConsumer<traffic_ipc::TrafficSnapshot> consumer{
      kTestQueue, kTestLock};
  if (consumer.open() != traffic_ipc::QueueStatus::kSuccess) {
    cleanup();
    return 3;
  }
  traffic_ipc::TrafficSnapshot newest{};
  if (consumer.receiveLatest(newest) != traffic_ipc::QueueStatus::kSuccess ||
      newest.frameId != 6U) {
    cleanup();
    return 4;
  }

  consumer.close();
  publisher.close();
  cleanup();
  return 0;
}
