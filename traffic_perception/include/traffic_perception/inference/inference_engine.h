#ifndef INFERENCE_INFERENCE_ENGINE_H
#define INFERENCE_INFERENCE_ENGINE_H

#include "traffic_perception/core/types.h"
#include "traffic_perception/inference/model_backend.h"
#include "traffic_perception/ingestion/atomic_frame_buffer.h"

namespace traffic_perception {

// Forward declaration of PipelineNode to avoid circular dependency
struct PipelineNode;

class InferenceEngine {
 private:
  AppConfig Config;
  ModelBackend Model;
  [[maybe_unused]] std::array<AtomicFrameBuffer *, 4> ActiveBuffers;

 public:
  PipelineNode *NextStage;

  static void consumerRoundRobinLoop();
  bool consumeFromBuffer(std::size_t laneId, FrameContext &ctx);
  void executeInference(FrameContext &ctx);
};

}  // namespace traffic_perception

#endif  // INFERENCE_INFERENCE_ENGINE_H
