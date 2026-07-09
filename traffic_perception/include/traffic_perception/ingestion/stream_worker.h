#ifndef INGESTION_STREAM_WORKER_H
#define INGESTION_STREAM_WORKER_H

#include <cstdint>
#include <opencv2/opencv.hpp>

#include "traffic_perception/core/types.h"
#include "traffic_perception/ingestion/safe_frame_queue.h"

class StreamWorker {
 private:
  int32_t LaneId;
  AppConfig Config;
  cv::VideoCapture RtspStream;

 public:
  bool initStream(AppConfig config, int32_t streamId);
  void producerLoop(SafeFrameQueue &queue) const;
  int32_t getHealthStatus() const;
};

#endif  // INGESTION_STREAM_WORKER_H
