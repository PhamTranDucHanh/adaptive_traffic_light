#include "traffic_perception/inference/inference_engine.h"

#include <iostream>

namespace traffic_perception {

void InferenceEngine::consumerRoundRobinLoop() {
  std::cout << "[InferenceEngine] consumerRoundRobinLoop() called" << '\n';
}

bool InferenceEngine::consumeFromBuffer(std::size_t laneId, FrameContext &ctx) {
    if (laneId >= ActiveBuffers.size() || ActiveBuffers[laneId] == nullptr) return false;
    Frame* frame = ActiveBuffers[laneId]->consume(static_cast<uint32_t>(laneId));
    if (frame == nullptr) return false;
    
    ctx.CapturedFrame = frame;
    ctx.LaneId = static_cast<int32_t>(laneId);
    // Detections and VehicleCount should be reset or managed by pipeline
    ctx.Detections.clear();
    ctx.VehicleCount = 0;
    
    return true;
}

void InferenceEngine::executeInference(FrameContext &ctx) {
  std::cout << "[InferenceEngine] executeInference() called for LaneId: "
            << ctx.LaneId << '\n';
  if (ctx.CapturedFrame) {
    ctx.Detections = ModelBackend::detect(ctx.CapturedFrame->Image);
    ctx.VehicleCount = static_cast<int32_t>(ctx.Detections.size());
  }
}

}  // namespace traffic_perception
