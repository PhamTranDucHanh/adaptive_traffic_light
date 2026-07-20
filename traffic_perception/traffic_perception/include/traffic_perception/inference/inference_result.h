#ifndef TRAFFIC_PERCEPTION_INFERENCE_INFERENCE_RESULT_H_
#define TRAFFIC_PERCEPTION_INFERENCE_INFERENCE_RESULT_H_

#include <vector>
#include <string>
#include <opencv2/core.hpp>

namespace traffic_perception {

struct Detection {
  cv::Rect Box;
  float Confidence;
  int32_t ClassId;
  std::string ClassName;
};

struct InferenceResult {
  int32_t FrameId;
  int32_t LaneId;
  int64_t InferenceLatencyMs;
  std::vector<Detection> Detections;
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_INFERENCE_INFERENCE_RESULT_H_
