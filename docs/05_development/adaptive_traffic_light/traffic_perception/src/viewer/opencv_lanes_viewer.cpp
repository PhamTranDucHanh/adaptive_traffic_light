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
constexpr std::int64_t kSignalStateOpenRetryNs = 1'000'000'000LL;
constexpr std::uint64_t kSignalStateStaleAfterNs = 3'000'000'000ULL;

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

std::vector<Detection> ScaleDetections(const std::vector<Detection>& source,
                                       const cv::Size& sourceSize) {
  std::vector<Detection> scaled = source;
  if (sourceSize.width <= 0 || sourceSize.height <= 0) {
    return scaled;
  }

  const float scaleX = static_cast<float>(kLaneCanvasWidth) /
                       static_cast<float>(sourceSize.width);
  const float scaleY = static_cast<float>(kLaneCanvasHeight) /
                       static_cast<float>(sourceSize.height);
  for (auto& detection : scaled) {
    detection.Box.x = static_cast<int>(std::lround(detection.Box.x * scaleX));
    detection.Box.y = static_cast<int>(std::lround(detection.Box.y * scaleY));
    detection.Box.width =
        static_cast<int>(std::lround(detection.Box.width * scaleX));
    detection.Box.height =
        static_cast<int>(std::lround(detection.Box.height * scaleY));
  }
  return scaled;
}

std::vector<cv::Point> ScaleRoi(const Roi& roi, const cv::Size& sourceSize) {
  std::vector<cv::Point> scaled;
  if (sourceSize.width <= 0 || sourceSize.height <= 0) {
    return scaled;
  }

  const float scaleX = static_cast<float>(kLaneCanvasWidth) /
                       static_cast<float>(sourceSize.width);
  const float scaleY = static_cast<float>(kLaneCanvasHeight) /
                       static_cast<float>(sourceSize.height);
  scaled.reserve(roi.Points.size());
  for (const auto& point : roi.Points) {
    scaled.emplace_back(static_cast<int>(std::lround(point.x * scaleX)),
                        static_cast<int>(std::lround(point.y * scaleY)));
  }
  return scaled;
}

void DrawScaledRoi(cv::Mat& image, const Roi& roi,
                   const cv::Size& sourceSize) {
  const std::vector<cv::Point> scaledRoi = ScaleRoi(roi, sourceSize);
  if (!scaledRoi.empty()) {
    const std::vector<std::vector<cv::Point>> contours{scaledRoi};
    cv::polylines(image, contours, true, cv::Scalar(0, 255, 0), 1,
                  cv::LINE_AA);
  }
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

void DrawSignalOverlay(
    cv::Mat& image, const Direction direction,
    const traffic_ipc::SignalStateMessageV1* const state,
    const bool fresh) {
  constexpr int kPanelWidth = 62;
  constexpr int kPanelHeight = 146;
  constexpr int kPanelMargin = 10;
  constexpr int kPanelTop = 92;
  constexpr int kLampRadius = 11;

  const int left = image.cols - kPanelWidth - kPanelMargin;
  if (left < 0 || kPanelTop + kPanelHeight > image.rows) {
    return;
  }

  cv::rectangle(image,
                cv::Rect(left, kPanelTop, kPanelWidth, kPanelHeight),
                cv::Scalar(18, 18, 18), cv::FILLED, cv::LINE_AA);
  cv::rectangle(image,
                cv::Rect(left, kPanelTop, kPanelWidth, kPanelHeight),
                cv::Scalar(185, 185, 185), 1, cv::LINE_AA);

  traffic_ipc::SignalLamp activeLamp = traffic_ipc::SignalLamp::kRed;
  if (fresh && state != nullptr) {
    activeLamp = direction == Direction::North || direction == Direction::South
                     ? state->northSouthLamp
                     : state->eastWestLamp;
  }

  const int centerX = left + (kPanelWidth / 2);
  const auto drawLamp = [&](const int centerY,
                            const traffic_ipc::SignalLamp lamp,
                            const cv::Scalar& bright,
                            const cv::Scalar& dim) {
    cv::circle(image, cv::Point(centerX, centerY), kLampRadius,
               fresh && activeLamp == lamp ? bright : dim, cv::FILLED,
               cv::LINE_AA);
    cv::circle(image, cv::Point(centerX, centerY), kLampRadius,
               cv::Scalar(120, 120, 120), 1, cv::LINE_AA);
  };

  drawLamp(kPanelTop + 24, traffic_ipc::SignalLamp::kRed,
           cv::Scalar(0, 0, 255), cv::Scalar(0, 0, 60));
  drawLamp(kPanelTop + 57, traffic_ipc::SignalLamp::kYellow,
           cv::Scalar(0, 230, 255), cv::Scalar(0, 55, 60));
  drawLamp(kPanelTop + 90, traffic_ipc::SignalLamp::kGreen,
           cv::Scalar(0, 220, 0), cv::Scalar(0, 55, 0));

  std::string countdown{"OFF"};
  if (fresh && state != nullptr) {
    const std::uint32_t remainingTimeMs =
        direction == Direction::North || direction == Direction::South
            ? state->northSouthRemainingTimeMs
            : state->eastWestRemainingTimeMs;
    const std::uint32_t remainingSeconds =
        (remainingTimeMs + 999U) / 1000U;
    countdown = std::to_string(remainingSeconds) + "s";
  }

  int baseline{};
  const cv::Size textSize = cv::getTextSize(
      countdown, cv::FONT_HERSHEY_SIMPLEX, 0.44, 1, &baseline);
  cv::putText(image, countdown,
              cv::Point(centerX - (textSize.width / 2), kPanelTop + 128),
              cv::FONT_HERSHEY_SIMPLEX, 0.44,
              fresh ? cv::Scalar(255, 255, 255)
                    : cv::Scalar(130, 130, 130),
              1, cv::LINE_AA);
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
  baseCanvas_.release();

  signalStateConsumer_.close();
  signalState_ = traffic_ipc::SignalStateMessageV1{};
  nextSignalStateOpenAttemptNs_ = 0;
  hasSignalState_ = false;

  return true;
}

void OpenCVLanesViewer::pollSignalState(const std::int64_t nowNs) noexcept {
  if (!signalStateConsumer_.isOpen()) {
    if (nowNs < nextSignalStateOpenAttemptNs_) {
      return;
    }
    nextSignalStateOpenAttemptNs_ = nowNs + kSignalStateOpenRetryNs;
    if (signalStateConsumer_.open() !=
        traffic_ipc::QueueStatus::kSuccess) {
      return;
    }
  }

  traffic_ipc::SignalStateMessageV1 candidate{};
  const auto status = signalStateConsumer_.receiveLatest(candidate);
  if (status == traffic_ipc::QueueStatus::kSuccess &&
      traffic_ipc::HasValidSignalStateEnvelope(candidate)) {
    signalState_ = candidate;
    hasSignalState_ = true;
  } else if (status == traffic_ipc::QueueStatus::kContractMismatch ||
             status == traffic_ipc::QueueStatus::kSystemError) {
    signalStateConsumer_.close();
  }
}

void OpenCVLanesViewer::refreshSignalOverlay(const std::int64_t nowNs) {
  pollSignalState(nowNs);
  if (baseCanvas_.empty()) {
    return;
  }

  const std::uint64_t currentNs =
      nowNs > 0 ? static_cast<std::uint64_t>(nowNs) : 0U;
  const bool signalStateFresh =
      hasSignalState_ && currentNs >= signalState_.publishTimestampNs &&
      (currentNs - signalState_.publishTimestampNs) <=
          kSignalStateStaleAfterNs;

  traffic_ipc::SignalStateMessageV1 displayState = signalState_;
  if (signalStateFresh) {
    const std::uint64_t ageMs =
        (currentNs - displayState.publishTimestampNs) / 1'000'000ULL;
    const auto subtractAge = [ageMs](const std::uint32_t remainingMs) {
      return ageMs >= remainingMs
                 ? 0U
                 : remainingMs - static_cast<std::uint32_t>(ageMs);
    };
    displayState.northSouthRemainingTimeMs =
        subtractAge(displayState.northSouthRemainingTimeMs);
    displayState.eastWestRemainingTimeMs =
        subtractAge(displayState.eastWestRemainingTimeMs);
  }

  cv::Mat canvas = baseCanvas_.clone();
  for (std::size_t i = 0; i < NUM_LANES; ++i) {
    const int left = static_cast<int>(i % 2U) * kLaneCanvasWidth;
    const int top = static_cast<int>(i / 2U) * kLaneCanvasHeight;
    cv::Mat lane = canvas(cv::Rect(left, top, kLaneCanvasWidth,
                                  kLaneCanvasHeight));
    DrawSignalOverlay(lane, config_.lanes[i].direction,
                      hasSignalState_ ? &displayState : nullptr,
                      signalStateFresh);
  }
  cv::imshow(windowName_, canvas);
}

void OpenCVLanesViewer::render(Analyzer& analyzer, int64_t expectedWakeupNs,
                               int64_t renderBeginNs) {
  const std::array<cv::Mat, NUM_LANES> noPreviews{};
  render(analyzer, noPreviews, expectedWakeupNs, renderBeginNs);
}

void OpenCVLanesViewer::render(
    Analyzer& analyzer,
    const std::array<cv::Mat, NUM_LANES>& previewFrames,
    int64_t expectedWakeupNs, int64_t renderBeginNs) {
  static std::atomic<std::int64_t> cycleId{0};
  const std::int64_t currentCycle = ++cycleId;

  auto frames = analyzer.takeRenderFrames();

  std::array<cv::Mat, NUM_LANES> canvasLanes{};

  for (std::size_t i = 0; i < NUM_LANES; ++i) {
    if (frames[i] != nullptr && !frames[i]->Image.empty()) {
      const auto& ctx = analyzer.latestContext(i);
      const cv::Size sourceSize = frames[i]->Image.size();
      cv::Mat displayFrame;
      cv::resize(frames[i]->Image, displayFrame,
                 cv::Size(kLaneCanvasWidth, kLaneCanvasHeight), 0.0, 0.0,
                 cv::INTER_AREA);

      if (backend_ != nullptr) {
        InferenceResult result;
        result.Detections = ScaleDetections(ctx.Detections, sourceSize);
        backend_->draw(displayFrame, result);
      }

      DrawScaledRoi(displayFrame, config_.lanes[i].roi, sourceSize);
      DrawMetricsOverlay(displayFrame, laneLabels_[i], ctx);
      lastRenderedFrames_[i] = std::move(displayFrame);

      analyzer.releaseFrame(frames[i]);
    } else if (lastRenderedFrames_[i].empty() &&
               !previewFrames[i].empty()) {
      // Preview is display-only and is used until the first analyzed frame for
      // this lane arrives. Domain metrics and published snapshots still come
      // exclusively from the inference pipeline.
      const cv::Size sourceSize = previewFrames[i].size();
      cv::resize(previewFrames[i], lastRenderedFrames_[i],
                 cv::Size(kLaneCanvasWidth, kLaneCanvasHeight), 0.0, 0.0,
                 cv::INTER_AREA);
      DrawScaledRoi(lastRenderedFrames_[i], config_.lanes[i].roi, sourceSize);
      cv::putText(lastRenderedFrames_[i], laneLabels_[i] + " | Preview",
                  cv::Point(8, 20), cv::FONT_HERSHEY_SIMPLEX, 0.42,
                  cv::Scalar(0, 255, 255), 1, cv::LINE_AA);
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

  cv::hconcat(canvasLanes[0], canvasLanes[1], top);
  cv::hconcat(canvasLanes[2], canvasLanes[3], bottom);
  cv::vconcat(top, bottom, baseCanvas_);

  refreshSignalOverlay(renderBeginNs);

  const int64_t renderEnd = GetMonotonicTimeNs();

  getViewerBenchmarkLogger().LogInfo()
      << "CycleId=" << currentCycle
      << " ExpectedWakeup=" << expectedWakeupNs
      << " Begin=" << renderBeginNs << " End=" << renderEnd;
}

void OpenCVLanesViewer::shutdown() {
  signalStateConsumer_.close();
  baseCanvas_.release();
  cv::destroyWindow(windowName_);
}

}  // namespace traffic_perception
