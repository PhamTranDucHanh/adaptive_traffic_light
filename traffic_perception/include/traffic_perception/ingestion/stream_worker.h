#ifndef INGESTION_STREAM_WORKER_H
#define INGESTION_STREAM_WORKER_H

#include <cstdint>
#include <opencv2/opencv.hpp>
#include <utility>

#include "traffic_perception/core/types.h"
#include "traffic_perception/ingestion/atomic_frame_buffer.h"

namespace traffic_perception {

class StreamWorker {
 private:
  int32_t LaneId;
  AppConfig Config;
  cv::VideoCapture RtspStream;

 public:
  bool initStream(AppConfig config, int32_t streamId);
  void producerLoop(AtomicFrameBuffer &frameBuffer) const;
  int32_t getHealthStatus() const;
};

}  // namespace traffic_perception

#endif  // INGESTION_STREAM_WORKER_H
