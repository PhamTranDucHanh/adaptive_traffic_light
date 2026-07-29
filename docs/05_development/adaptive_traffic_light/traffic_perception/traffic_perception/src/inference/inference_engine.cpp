#include "traffic_perception/inference/inference_engine.h"

#include <iostream>

#include "score/mw/log/logging.h"

namespace traffic_perception {

InferenceEngine::InferenceEngine(IModelBackend& backend,
                                 AtomicFrameBuffer& buffer, FramePool& pool,
                                 IInferenceSink& sink,
                                 const std::array<Roi, NUM_LANES>& laneRois)
    : backend_(backend),
      buffer_(buffer),
      pool_(pool),
      sink_(sink),
      laneRois_(laneRois) {}

void InferenceEngine::runOneCycle() {
  for (uint32_t laneId = 0; laneId < 4; ++laneId) {
    // 1. Ownership Transfer: Engine takes Frame from Buffer
    // Ownership transferred from AtomicFrameBuffer
    Frame* frame = buffer_.take(laneId);

    // 2. Skip if no frame
    if (frame == nullptr) {
      continue;
    }

    score::mw::log::LogDebug()
        << "[InferenceEngine] Received FrameId: " << frame->FrameId
        << " Lane: " << laneId << "\n";

    // 3. Perform Inference
    frame->timeline.add(TimelineStage::InferenceBegin);
    InferenceResult result = backend_.infer(*frame);
    frame->timeline.add(TimelineStage::InferenceEnd);

    // 4. Vehicle Filtering — only vehicle detections reach the Analyzer
    {
      std::vector<Detection> vehicleDetections;
      for (const auto& det : result.Detections) {
        if (det.IsVehicle) {
          vehicleDetections.push_back(det);
        }
      }
      result.Detections = std::move(vehicleDetections);
    }

    // 5. ROI Filtering
    const auto& roi = laneRois_[laneId];
    if (!roi.Points.empty()) {
      std::vector<Detection> filteredDetections;
      for (const auto& det : result.Detections) {
        cv::Point2f bottomCenter(det.Box.x + det.Box.width * 0.5f,
                                 det.Box.y + det.Box.height);

        // Use OpenCV pointPolygonTest to check if inside ROI
        if (cv::pointPolygonTest(roi.Points, bottomCenter, false) >= 0) {
          filteredDetections.push_back(det);
        }
      }
      result.Detections = std::move(filteredDetections);
    }

    result.LaneId = static_cast<int32_t>(laneId);
    score::mw::log::LogDebug()
        << "[InferenceEngine] Inferenced FrameId: " << frame->FrameId
        << " Lane: " << laneId << " Detections: " << result.Detections.size()
        << "\n";

    // 5. Ownership Transfer: Handoff Frame + Result to Sink
    sink_.accept(frame, std::move(result));

    // Note: InferenceEngine no longer owns 'frame'.
    // It MUST NOT release or access it further.
  }
}

}  // namespace traffic_perception
