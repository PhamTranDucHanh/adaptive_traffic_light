#ifndef TRAFFIC_PERCEPTION_CORE_TYPES_H_
#define TRAFFIC_PERCEPTION_CORE_TYPES_H_

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

struct Roi {
  std::vector<cv::Point> Points;
  std::string LaneId;
};

struct Detection {
  cv::Rect Box;
  std::int32_t ClassId{};
  float Confidence{};
  std::string ClassName;
};

struct Frame {
  std::int32_t FrameId{};
  cv::Mat Image{};
};

#include <chrono>
struct FrameContext {
  Frame* CapturedFrame{};
  std::int32_t LaneId{};
  std::vector<Detection> Detections;
  std::chrono::steady_clock::time_point Timestamp;
  std::uint32_t VehicleCount{};
  float Occupancy{};
  float QueueLength{};
  bool EmergencyDetected{};
};

struct AppConfig {
  std::array<std::string, 4U> trafficVidSources{};
  std::array<Roi, 4U> laneRois{};
  std::string ModelPath;
  std::int32_t MaxQueueSize{1};
  std::int32_t TargetFps{};
};

// Temporary assumption: Van is the emergency vehicle
constexpr std::int32_t kEmergencyVehicleClassId = 1; // Assuming 1 maps to 'Van'

struct TrafficSnapshot {
  std::int32_t frameId{};
  std::int64_t timestampUs{};

  std::int32_t vehicleCountNorth{};
  std::int32_t vehicleCountSouth{};
  std::int32_t vehicleCountEast{};
  std::int32_t vehicleCountWest{};

  float queueLengthNorth{0.0f};
  float queueLengthSouth{0.0f};
  float queueLengthEast{0.0f};
  float queueLengthWest{0.0f};

  float occupancyNorth{0.0f};
  float occupancySouth{0.0f};
  float occupancyEast{0.0f};
  float occupancyWest{0.0f};

  bool emergencyNorth{false};
  bool emergencySouth{false};
  bool emergencyEast{false};
  bool emergencyWest{false};
};

#endif // TRAFFIC_PERCEPTION_CORE_TYPES_H_
