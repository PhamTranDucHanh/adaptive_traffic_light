#ifndef TRAFFIC_PERCEPTION_INGESTION_STREAM_WORKER_H_
#define TRAFFIC_PERCEPTION_INGESTION_STREAM_WORKER_H_

#include <chrono>
#include <cstdint>
#include <opencv2/opencv.hpp>
#include <string>
#include <utility>

#include "traffic_perception/core/frame_pool.h"
#include "traffic_perception/core/types.h"
#include "traffic_perception/ingestion/atomic_frame_buffer.h"

namespace traffic_perception {

enum class InputSourceType : uint8_t { LocalFile, RtspStream, Camera, Unknown };

class StreamWorker {
 public:
  bool initStream(std::string sourceUri, int32_t streamId, FramePool* pool,
                  std::chrono::milliseconds period,
                  std::chrono::milliseconds phase);

  void run(AtomicFrameBuffer& frameBuffer,
           std::chrono::steady_clock::time_point startTime);
  void stop();

  int32_t getHealthStatus() const;

  int32_t getLaneId() const;

 private:
  int32_t LaneId;
  std::string SourceUri;
  FramePool* Pool;

  std::atomic<bool> running_{true};

  std::chrono::milliseconds AcquisitionPeriod{200};
  std::chrono::milliseconds phase_{0};

  static InputSourceType detectSourceType(const std::string& source);
  static cv::VideoCapture createCapture(const std::string& source);
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_INGESTION_STREAM_WORKER_H_
