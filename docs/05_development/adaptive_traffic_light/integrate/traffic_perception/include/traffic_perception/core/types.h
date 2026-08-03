#ifndef TRAFFIC_PERCEPTION_CORE_TYPES_H_
#define TRAFFIC_PERCEPTION_CORE_TYPES_H_

#include <array>
#include <cstdint>
#include <chrono>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

enum class Direction : uint8_t {
  North = 0,
  South,
  East,
  West
};

struct Roi {
  std::vector<cv::Point> Points;
  std::string LaneId;
};

struct Detection {
  cv::Rect Box;
  std::int32_t ClassId{};
  float Confidence{};
  std::string ClassName;
  bool IsVehicle{false};
  bool IsEmergency{false};
};

enum class TimelineStage {
  Capture,
  InferenceBegin,
  InferenceEnd,
  AnalyzerBegin,
  AnalyzerEnd,
  Publish
};

struct TimelineEntry {
  TimelineStage stage;
  std::chrono::steady_clock::time_point timestamp;
};

struct Timeline {
  std::int32_t frameId{0};
  std::vector<TimelineEntry> entries;

  void add(TimelineStage stage) {
    entries.push_back({stage, std::chrono::steady_clock::now()});
  }
};

struct Frame {
  std::int32_t FrameId{};
  cv::Mat Image{};
  Timeline timeline;
};

struct LaneConfig {
  Direction direction;
  Roi roi;
  std::string videoSource;
};


constexpr std::size_t NUM_LANES = 4;

struct ThreadConfig {
  std::string Policy{"SCHED_RR"};
  std::int32_t Priority{0};
};

struct ThreadingConfig {
  ThreadConfig Stream;
  ThreadConfig Pipeline;
};

struct AppConfig {
  std::array<LaneConfig, NUM_LANES> lanes{};
  std::string modelPath;

  std::chrono::milliseconds CapturePeriod{200};
  std::chrono::milliseconds PipelinePeriod{3000};
  std::chrono::milliseconds ViewerPeriod{3000};
  std::chrono::milliseconds CapturePhase{0};
  std::chrono::milliseconds PipelinePhase{50};
  std::chrono::milliseconds ViewerPhase{260};

  ThreadingConfig Threading;
};

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

struct FrameContext {
  Timeline timeline;
  TrafficSnapshot snapshot;
};

#endif // TRAFFIC_PERCEPTION_CORE_TYPES_H_
