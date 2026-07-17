#ifndef INGESTION_SAFE_FRAME_QUEUE_H
#define INGESTION_SAFE_FRAME_QUEUE_H

#include <mutex>
#include <queue>
#include <utility>

#include "traffic_perception/core/types.h"

namespace traffic_perception {

class SafeFrameQueue {
 private:
  std::mutex Mutex;
  std::queue<FrameContext> InternalQueue;

 public:
  void enqueue_with_overwrite(FrameContext &&ctx);
  bool dequeue_non_blocking(FrameContext &ctx);
};

}  // namespace traffic_perception

#endif  // INGESTION_SAFE_FRAME_QUEUE_H
