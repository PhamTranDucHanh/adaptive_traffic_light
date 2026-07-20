#ifndef TRAFFIC_PERCEPTION_PIPELINE_ANALYZER_H_
#define TRAFFIC_PERCEPTION_PIPELINE_ANALYZER_H_

#include <atomic>
#include <array>
#include <opencv2/opencv.hpp>
#include "traffic_perception/inference/inference_sink.h"
#include "traffic_perception/core/frame_pool.h"
#include "traffic_perception/core/types.h"

namespace traffic_perception {

class Analyzer : public IInferenceSink {
 public:
  explicit Analyzer(FramePool& pool) : pool_(pool) {
      for(auto& f : renderFrames_) f.store(nullptr);
  }
  
  // Analyzer accepts ownership of the frame
  void accept(Frame* frame, InferenceResult&& result) override {
    if (!frame) return;

    // Perform analysis and draw overlays directly on frame
    for (const auto& det : result.Detections) {
      cv::rectangle(frame->Image, det.Box, cv::Scalar(0, 255, 0), 2);
    }
    
    // Store as latest frame for the specific lane
    // Note: This assumes result.LaneId is available. 
    // If not, we might need to derive it from frame or context.
    // Use the lane ID provided by inference result.
    uint32_t lane = static_cast<uint32_t>(result.LaneId); 
    
    if (lane >= NUM_LANES) return; // Basic validation
    
    Frame* old = renderFrames_[lane].exchange(frame);
    if (old != nullptr) {
        pool_.release(old);
    }
  }

  // Atomically retrieve frames for all lanes for rendering
  std::array<Frame*, NUM_LANES> takeRenderFrames() {
    std::array<Frame*, NUM_LANES> frames;
    for (uint32_t i = 0; i < NUM_LANES; ++i) {
        frames[i] = renderFrames_[i].exchange(nullptr);
    }
    return frames;
  }

  void releaseFrame(Frame* frame) {
      pool_.release(frame);
  }

 private:
  FramePool& pool_;
  std::array<std::atomic<Frame*>, NUM_LANES> renderFrames_{};
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_PIPELINE_ANALYZER_H_
