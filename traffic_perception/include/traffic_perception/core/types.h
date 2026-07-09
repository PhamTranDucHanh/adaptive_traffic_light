#ifndef TYPES_H
#define TYPES_H

#include <cstdint>
#include <opencv2/opencv.hpp>

struct Detection {
  cv::Rect Box;
  int32_t ClassId;
  float Confidence;
};

struct FrameContext {
  int32_t FrameId;
  cv::Mat Frame;
  std::vector<Detection> Detections;
  int32_t LaneId;
  int32_t VehicleCount;
};

struct AppConfig {
  std::array<std::string, 4> RtspUrls;
  std::string ModelPath;
  int32_t MaxQueueSize = 1;
  int32_t TargetFps;
};

#endif  // TYPES_H
