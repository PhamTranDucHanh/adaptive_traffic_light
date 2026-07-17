#include "traffic_perception/ingestion/safe_frame_queue.h"

#include <iostream>

namespace traffic_perception {

void SafeFrameQueue::enqueue_with_overwrite(FrameContext &&ctx) {
  std::lock_guard<std::mutex> lock(Mutex);
  std::cout << "[SafeFrameQueue] enqueue_with_overwrite() called for FrameId: "
            << ctx.FrameId << '\n';
  if (!InternalQueue.empty()) {
    InternalQueue.pop();  // Discard old frame
  }
  InternalQueue.push(std::move(ctx));
}

bool SafeFrameQueue::dequeue_non_blocking(FrameContext &ctx) {
  std::lock_guard<std::mutex> lock(Mutex);
  if (InternalQueue.empty()) {
    return false;
  }
  ctx = std::move(InternalQueue.front());
  InternalQueue.pop();
  std::cout
      << "[SafeFrameQueue] dequeue_non_blocking() called, retrieved FrameId: "
      << ctx.FrameId << '\n';
  return true;
}

}  // namespace traffic_perception
