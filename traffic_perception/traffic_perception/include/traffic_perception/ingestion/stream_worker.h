#ifndef INGESTION_STREAM_WORKER_H
#define INGESTION_STREAM_WORKER_H

#include <cstdint>
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>
#include <atomic>
#include <utility>

#include "traffic_perception/core/types.h"
#include "traffic_perception/core/frame_pool.h"
#include "traffic_perception/ingestion/atomic_frame_buffer.h"

namespace traffic_perception {

enum class InputSourceType {
  LocalFile,
  RtspStream,
  Camera,
  Unknown
};

class StreamWorker {
 private:
  int32_t LaneId;
  std::string SourceUri;
  FramePool* Pool;
  AtomicFrameBuffer* Buffer{nullptr};
  std::thread WorkerThread;
  std::atomic<bool> Running{false};
  std::chrono::milliseconds AcquisitionPeriod{33};

  static InputSourceType detectSourceType(const std::string& source);
  static cv::VideoCapture createCapture(const std::string& source);

 public:
  ~StreamWorker();
  bool initStream(std::string sourceUri, int32_t streamId, FramePool* pool, std::chrono::milliseconds period = std::chrono::milliseconds(200));
  bool restart();
  void start(AtomicFrameBuffer &frameBuffer);
  void stop();
  void producerLoop(AtomicFrameBuffer &frameBuffer);
  int32_t getHealthStatus() const;
};

}  // namespace traffic_perception

#endif  // INGESTION_STREAM_WORKER_H
