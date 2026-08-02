#include "traffic_perception/viewer/opencv_lanes_viewer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

#include "score/mw/log/logger.h"
#include "traffic_perception/core/time_utils.h"

namespace {

constexpr int kLaneCanvasWidth = 640;
constexpr int kLaneCanvasHeight = 360;

inline score::mw::log::Logger& getViewerBenchmarkLogger() {
  static score::mw::log::Logger& logger =
      score::mw::log::CreateLogger("VIEW", "Traffic Perception Viewer");
  return logger;
}

const char* DirectionName(Direction direction) {
  switch (direction) {
    case Direction::North:
      return "NORTH";
    case Direction::South:
      return "SOUTH";
    case Direction::East:
      return "EAST";
    case Direction::West:
      return "WEST";
  }
  return "UNKNOWN";
}

std::string FileName(const std::string& path) {
  const std::size_t separator = path.find_last_of("/\\");
  return separator == std::string::npos ? path : path.substr(separator + 1U);
}

void DrawMetricsOverlay(
    cv::Mat& image, const std::string& laneLabel,
    const traffic_perception::Analyzer::LaneMetrics& metrics) {
  const float uiScale = std::clamp(
      std::min(static_cast<float>(image.cols) / kLaneCanvasWidth,
               static_cast<float>(image.rows) / kLaneCanvasHeight),
      0.75F, 3.0F);
  const auto scaled = [uiScale](int value) {
    return static_cast<int>(std::lround(static_cast<float>(value) * uiScale));
  };

  const int panelHeight = std::min(scaled(82), image.rows);
  cv::Mat panel = image(cv::Rect(0, 0, image.cols, panelHeight));
  const cv::Mat panelBackground(panel.size(), panel.type(),
                                cv::Scalar(24, 24, 24));
  cv::addWeighted(panel, 0.35, panelBackground, 0.65, 0.0, panel);

  cv::putText(image, laneLabel, cv::Point(scaled(12), scaled(24)),
              cv::FONT_HERSHEY_SIMPLEX, 0.64 * uiScale,
              cv::Scalar(0, 255, 255), std::max(2, scaled(2)), cv::LINE_AA);

  std::ostringstream trafficMetrics;
  trafficMetrics << "Vehicles: " << metrics.VehicleCount << "  Queue: "
                 << std::fixed << std::setprecision(1)
                 << metrics.QueueLength * 100.0F << '%';
  cv::putText(image, trafficMetrics.str(), cv::Point(scaled(12), scaled(51)),
              cv::FONT_HERSHEY_SIMPLEX, 0.58 * uiScale,
              cv::Scalar(255, 255, 255), std::max(1, scaled(1)), cv::LINE_AA);

  std::ostringstream statusMetrics;
  statusMetrics << "Occupancy: " << std::fixed << std::setprecision(1)
                << metrics.Occupancy * 100.0F << "%  Emergency: "
                << (metrics.EmergencyDetected ? "YES" : "NO");
  cv::putText(image, statusMetrics.str(), cv::Point(scaled(12), scaled(76)),
              cv::FONT_HERSHEY_SIMPLEX, 0.58 * uiScale,
              metrics.EmergencyDetected ? cv::Scalar(0, 80, 255)
                                        : cv::Scalar(120, 255, 120),
              std::max(1, scaled(1)), cv::LINE_AA);
}

}  // namespace

namespace traffic_perception {

bool OpenCVLanesViewer::init(const AppConfig& config, IModelBackend* backend) {
  config_ = config;
  backend_ = backend;
  windowName_ = "Traffic Perception - 4 Directions";

  for (std::size_t i = 0; i < NUM_LANES; ++i) {
    laneLabels_[i] =
        "Road: " + std::string(DirectionName(config_.lanes[i].direction)) +
        " | Video: " + FileName(config_.lanes[i].videoSource);
  }

  cv::namedWindow(windowName_, cv::WINDOW_AUTOSIZE);

  for (auto& frame : lastRenderedFrames_) {
    frame = cv::Mat();
  }

  return true;
}

void OpenCVLanesViewer::render(Analyzer& analyzer, int64_t expectedWakeupNs,
                               int64_t renderBeginNs) {
  auto frames = analyzer.takeRenderFrames();

  std::array<cv::Mat, NUM_LANES> canvasLanes{};

  for (std::size_t i = 0; i < NUM_LANES; ++i) {
    if (frames[i] != nullptr && !frames[i]->Image.empty()) {
      const auto& ctx = analyzer.latestContext(i);

      if (backend_ != nullptr) {
        InferenceResult result;
        result.Detections = ctx.Detections;
        backend_->draw(frames[i]->Image, result);
      }

      const auto& roi = config_.lanes[i].roi;
      std::vector<std::vector<cv::Point>> contours = {roi.Points};

      DrawMetricsOverlay(frames[i]->Image, laneLabels_[i], ctx);
      const float roiScale = std::clamp(
          std::min(static_cast<float>(frames[i]->Image.cols) / kLaneCanvasWidth,
                   static_cast<float>(frames[i]->Image.rows) /
                       kLaneCanvasHeight),
          1.0F, 3.0F);
      cv::polylines(frames[i]->Image, contours, true, cv::Scalar(0, 255, 0),
                    std::max(2, static_cast<int>(std::lround(2.0F * roiScale))),
                    cv::LINE_AA);

      cv::resize(frames[i]->Image, lastRenderedFrames_[i],
                 cv::Size(kLaneCanvasWidth, kLaneCanvasHeight));

      analyzer.releaseFrame(frames[i]);
    }

    if (!lastRenderedFrames_[i].empty()) {
      canvasLanes[i] = lastRenderedFrames_[i];
    } else {
      canvasLanes[i] = cv::Mat(kLaneCanvasHeight, kLaneCanvasWidth, CV_8UC3,
                               cv::Scalar(0, 0, 0));

      cv::putText(canvasLanes[i], laneLabels_[i] + " - Waiting...",
                  cv::Point(35, kLaneCanvasHeight / 2),
                  cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255, 255, 255), 2,
                  cv::LINE_AA);
    }
  }

  cv::Mat top;
  cv::Mat bottom;
  cv::Mat canvas;

  cv::hconcat(canvasLanes[0], canvasLanes[1], top);
  cv::hconcat(canvasLanes[2], canvasLanes[3], bottom);
  cv::vconcat(top, bottom, canvas);

  cv::imshow(windowName_, canvas);

  const int64_t renderEnd = GetMonotonicTimeNs();

  getViewerBenchmarkLogger().LogInfo()
      << "ExpectedWakeup=" << expectedWakeupNs
      << " Begin=" << renderBeginNs << " End=" << renderEnd;
}

void OpenCVLanesViewer::shutdown() { cv::destroyWindow(windowName_); }

}  // namespace traffic_perception
