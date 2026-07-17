#include "traffic_perception/ingestion/stream_worker.h"

#include <iostream>

namespace traffic_perception {

bool StreamWorker::initStream(AppConfig config, int32_t streamId) {
  Config = std::move(config);
  LaneId = streamId;
  std::cout << "[StreamWorker] initStream() called for Lane " << LaneId << '\n';
  return true;
}

void StreamWorker::producerLoop(SafeFrameQueue &queue) const {
  (void)queue;
  std::cout << "[StreamWorker] producerLoop() called for Lane " << LaneId
            << '\n';
}

int32_t StreamWorker::getHealthStatus() const {
  std::cout << "[StreamWorker] getHealthStatus() called for Lane " << LaneId
            << '\n';
  return 1;  // 1 = healthy
}

}  // namespace traffic_perception
