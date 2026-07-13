#include "traffic_perception/inference/inference_engine.h"

#include <iostream>

void InferenceEngine::consumerRoundRobinLoop() {
  std::cout << "[InferenceEngine] consumerRoundRobinLoop() called" << '\n';
}

void InferenceEngine::executeInference(FrameContext &ctx) {
  std::cout << "[InferenceEngine] executeInference() called for LaneId: "
            << ctx.LaneId << '\n';
  ctx.Detections = ModelBackend::detect(ctx.Frame);
  ctx.VehicleCount = static_cast<int32_t>(ctx.Detections.size());
}
