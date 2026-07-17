#ifndef TRAFFIC_PERCEPTION_CORE_TYPES_H_
#define TRAFFIC_PERCEPTION_CORE_TYPES_H_

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

struct Detection {
  cv::Rect Box{};
  std::int32_t ClassId{};
  float Confidence{};
};

struct FrameContext {
  std::int32_t FrameId{};
  cv::Mat Frame{};
  std::vector<Detection> Detections{};
  std::int32_t LaneId{};
  std::int32_t VehicleCount{};
};

struct AppConfig {
  std::array<std::string, 4U> RtspUrls{};
  std::string ModelPath{};
  std::int32_t MaxQueueSize{1};
  std::int32_t TargetFps{};
};

#endif  // TRAFFIC_PERCEPTION_CORE_TYPES_H_
