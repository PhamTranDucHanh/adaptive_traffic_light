#ifndef TRAFFIC_PERCEPTION_PIPELINE_ANALYZER_H_
#define TRAFFIC_PERCEPTION_PIPELINE_ANALYZER_H_

#include <algorithm>
#include <atomic>
#include <array>
#include <iostream>
#include <limits>
#include <utility>
#include <opencv2/opencv.hpp>
#include "traffic_perception/inference/inference_sink.h"
#include "traffic_perception/core/frame_pool.h"
#include "traffic_perception/core/types.h"
#include "traffic_perception/inference/inference_result.h"

namespace traffic_perception {

class Analyzer : public IInferenceSink {
 public:
  struct LaneMetrics {
      std::vector<Detection> Detections;
      std::uint32_t VehicleCount{0};
      float Occupancy{0.0f};
      float QueueLength{0.0f};
      bool EmergencyDetected{false};
      std::int32_t LaneId{0};
  };

  struct LaneState {
      LaneMetrics Metrics{};
      std::atomic<Frame*> LatestRenderFrame{nullptr};
  };

  Analyzer(FramePool& pool, const std::array<Roi, NUM_LANES>& laneRois)
      : pool_(pool), laneRois_(laneRois) {
  }
  
  // Analyzer accepts ownership of the frame and updates internal state
  void accept(Frame* frame, InferenceResult&& result) override {
    if (!frame) return;
    
    frame->timeline.add(TimelineStage::AnalyzerBegin);

    uint32_t lane = static_cast<uint32_t>(result.LaneId);
    if (lane < NUM_LANES) {
        // 1. Calculate lane metrics
        LaneMetrics metrics = buildLaneMetrics(frame, result);
        
        // 2. Update LaneState (internal perception state)
        laneStates_[lane].Metrics = std::move(metrics);
        logLaneMetrics(laneStates_[lane].Metrics);

        frame->timeline.add(TimelineStage::AnalyzerEnd);
        latestFrame_ = frame;
        latestTimeline_ = frame->timeline;

        // 3. Atomically update the render frame ownership
        Frame* old = laneStates_[lane].LatestRenderFrame.exchange(frame);
        if (old != nullptr) {
            pool_.release(old);
        }
    } else {
        frame->timeline.add(TimelineStage::AnalyzerEnd);
        // Fallback for invalid lane, release frame
        pool_.release(frame);
    }
  }

  // Atomically retrieve frames for all lanes for rendering
  std::array<Frame*, NUM_LANES> takeRenderFrames() {
    std::array<Frame*, NUM_LANES> frames;
    for (uint32_t i = 0; i < NUM_LANES; ++i) {
        frames[i] = laneStates_[i].LatestRenderFrame.exchange(nullptr);
    }
    return frames;
  }

  // Read-only access to latest metrics
  const LaneMetrics& latestContext(uint32_t lane) const {
      return laneStates_[lane].Metrics;
  }

  Frame* latestFrame() const { return latestFrame_; }
  const Timeline& latestTimeline() const { return latestTimeline_; }

  // Builder for TrafficSnapshot DTO
  TrafficSnapshot buildTrafficSnapshot() {
      TrafficSnapshot snapshot;
      
      // 1. Snapshot-level ID (monotonically increasing per snapshot construction)
      snapshot.frameId = ++frameCounter_;

      // 2. Snapshot-level timestamp (now)
      auto now = std::chrono::system_clock::now();
      snapshot.timestampUs = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

      // 3. Aggregate per-lane metrics
      for (uint32_t i = 0; i < NUM_LANES; ++i) {
          const auto& metrics = laneStates_[i].Metrics;

          switch (static_cast<Direction>(i)) {
              case Direction::North:
                  snapshot.vehicleCountNorth = static_cast<int32_t>(metrics.VehicleCount);
                  snapshot.occupancyNorth = metrics.Occupancy;
                  snapshot.queueLengthNorth = metrics.QueueLength;
                  snapshot.emergencyNorth = metrics.EmergencyDetected;
                  break;
              case Direction::South:
                  snapshot.vehicleCountSouth = static_cast<int32_t>(metrics.VehicleCount);
                  snapshot.occupancySouth = metrics.Occupancy;
                  snapshot.queueLengthSouth = metrics.QueueLength;
                  snapshot.emergencySouth = metrics.EmergencyDetected;
                  break;
              case Direction::East:
                  snapshot.vehicleCountEast = static_cast<int32_t>(metrics.VehicleCount);
                  snapshot.occupancyEast = metrics.Occupancy;
                  snapshot.queueLengthEast = metrics.QueueLength;
                  snapshot.emergencyEast = metrics.EmergencyDetected;
                  break;
              case Direction::West:
                  snapshot.vehicleCountWest = static_cast<int32_t>(metrics.VehicleCount);
                  snapshot.occupancyWest = metrics.Occupancy;
                  snapshot.queueLengthWest = metrics.QueueLength;
                  snapshot.emergencyWest = metrics.EmergencyDetected;
                  break;
          }
      }
      return snapshot;
  }

  void releaseFrame(Frame* frame) {
      pool_.release(frame);
  }

 private:
  // Dedicated conversion step from raw InferenceResult to internal LaneMetrics
  LaneMetrics buildLaneMetrics(Frame* frame, const InferenceResult& result) const {
      LaneMetrics metrics;
      metrics.LaneId = result.LaneId;
      metrics.Detections = result.Detections;
      metrics.VehicleCount = static_cast<uint32_t>(metrics.Detections.size());

      const uint32_t lane = static_cast<uint32_t>(result.LaneId);
      const Roi& roi = laneRois_[lane];
      cv::Rect roiBounds;
      if (!roi.Points.empty()) {
          roiBounds = cv::boundingRect(roi.Points);
      }

      for (const auto& det : metrics.Detections) {
          if (det.IsEmergency) {
              metrics.EmergencyDetected = true;
          }
      }

      if (roiBounds.width > 0 && roiBounds.height > 0) {
          cv::Mat roiMask(roiBounds.height, roiBounds.width, CV_8UC1, cv::Scalar(0));
          std::vector<cv::Point> shiftedRoi;
          shiftedRoi.reserve(roi.Points.size());
          const cv::Point roiOffset = roiBounds.tl();
          for (const auto& point : roi.Points) {
              shiftedRoi.push_back(point - roiOffset);
          }
          std::vector<std::vector<cv::Point>> roiContours{shiftedRoi};
          cv::fillPoly(roiMask, roiContours, cv::Scalar(255));

          cv::Mat vehicleMask(roiBounds.height, roiBounds.width, CV_8UC1, cv::Scalar(0));
          for (const auto& det : metrics.Detections) {
              const cv::Rect clippedBox = det.Box & roiBounds;
              if (clippedBox.width <= 0 || clippedBox.height <= 0) {
                  continue;
              }
              const cv::Rect localBox(clippedBox.x - roiBounds.x,
                                      clippedBox.y - roiBounds.y,
                                      clippedBox.width,
                                      clippedBox.height);
              cv::rectangle(vehicleMask, localBox, cv::Scalar(255), cv::FILLED);
          }

          cv::bitwise_and(vehicleMask, roiMask, vehicleMask);
          const int roiPixels = cv::countNonZero(roiMask);
          if (roiPixels > 0) {
              const int occupiedPixels = cv::countNonZero(vehicleMask);
              metrics.Occupancy = clampMetric(static_cast<float>(occupiedPixels) /
                                              static_cast<float>(roiPixels));
          }

          metrics.QueueLength = calculateQueueLength(metrics.Detections,
                                                     static_cast<float>(roiBounds.height));
      }

      return metrics;
  }

  static float clampMetric(float value) {
      return std::clamp(value, 0.0f, 1.0f);
  }

  static float calculateQueueLength(const std::vector<Detection>& detections,
                                    float roiHeight) {
      if (detections.empty() || roiHeight <= 0.0f) {
          return 0.0f;
      }

      std::vector<std::pair<float, float>> vehicles;
      vehicles.reserve(detections.size());
      float totalVehicleHeight = 0.0f;
      for (const auto& det : detections) {
          const float height = static_cast<float>(std::max(0, det.Box.height));
          const float bottomCenterY = static_cast<float>(det.Box.y + det.Box.height);
          vehicles.emplace_back(bottomCenterY, height);
          totalVehicleHeight += height;
      }

      std::sort(vehicles.begin(), vehicles.end(),
                [](const auto& lhs, const auto& rhs) {
                    return lhs.first < rhs.first;
                });

      if (vehicles.size() == 1U) {
          return clampMetric(vehicles.front().second / roiHeight);
      }

      const float averageVehicleHeight =
          totalVehicleHeight / static_cast<float>(vehicles.size());
      const float maxContinuousGap = 1.5f * averageVehicleHeight;

      std::size_t bestStart = 0U;
      std::size_t bestEnd = 0U;
      std::size_t currentStart = 0U;
      auto updateBestCluster = [&](std::size_t start, std::size_t end) {
          const std::size_t currentCount = end - start + 1U;
          const std::size_t bestCount = bestEnd - bestStart + 1U;
          const float currentSpan = vehicles[end].first - vehicles[start].first;
          const float bestSpan = vehicles[bestEnd].first - vehicles[bestStart].first;
          if (currentCount > bestCount ||
              (currentCount == bestCount && currentSpan > bestSpan)) {
              bestStart = start;
              bestEnd = end;
          }
      };

      for (std::size_t i = 1U; i < vehicles.size(); ++i) {
          const float verticalGap = vehicles[i].first - vehicles[i - 1U].first;
          if (averageVehicleHeight > 0.0f && verticalGap > maxContinuousGap) {
              updateBestCluster(currentStart, i - 1U);
              currentStart = i;
          }
      }
      updateBestCluster(currentStart, vehicles.size() - 1U);

      if (bestStart == bestEnd) {
          return clampMetric(vehicles[bestStart].second / roiHeight);
      }

      return clampMetric((vehicles[bestEnd].first - vehicles[bestStart].first) /
                         roiHeight);
  }

  static const char* laneName(std::int32_t lane) {
      switch (static_cast<Direction>(lane)) {
          case Direction::North:
              return "North";
          case Direction::South:
              return "South";
          case Direction::East:
              return "East";
          case Direction::West:
              return "West";
          default:
              return "Unknown";
      }
  }

  static void logLaneMetrics(const LaneMetrics& metrics) {
      std::cout << "----------------------------------------\n"
                << "[Analyzer]\n\n"
                << "Lane: " << laneName(metrics.LaneId) << "\n\n"
                << "Vehicles: " << metrics.VehicleCount << "\n\n"
                << "Occupancy: " << metrics.Occupancy << "\n\n"
                << "Queue Length: " << metrics.QueueLength << "\n\n"
                << "Emergency: " << (metrics.EmergencyDetected ? "YES" : "NO") << "\n\n";
  }

  FramePool& pool_;
  const std::array<Roi, NUM_LANES>& laneRois_;
  std::array<LaneState, NUM_LANES> laneStates_{};
  std::int32_t frameCounter_{0};
  Frame* latestFrame_{nullptr};
  Timeline latestTimeline_{};
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_PIPELINE_ANALYZER_H_
