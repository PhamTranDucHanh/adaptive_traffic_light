#ifndef INFERENCE_INFERENCE_ENGINE_H
#define INFERENCE_INFERENCE_ENGINE_H

#include "traffic_perception/core/types.h"
#include "traffic_perception/inference/model_backend.h"

namespace traffic_perception {

// Forward declaration of PipelineNode to avoid circular dependency
struct PipelineNode;
class SafeFrameQueue;

class InferenceEngine {
 private:
  AppConfig Config;
  ModelBackend Model;
  [[maybe_unused]] std::array<SafeFrameQueue *, 4> ActiveQueues;

 public:
  PipelineNode *NextStage;

  static void consumerRoundRobinLoop();
  void executeInference(FrameContext &ctx);
};

}  // namespace traffic_perception

#endif  // INFERENCE_INFERENCE_ENGINE_H
