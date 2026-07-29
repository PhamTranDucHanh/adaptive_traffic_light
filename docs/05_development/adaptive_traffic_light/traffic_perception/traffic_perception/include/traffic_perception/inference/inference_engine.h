#ifndef TRAFFIC_PERCEPTION_INFERENCE_INFERENCE_ENGINE_H_
#define TRAFFIC_PERCEPTION_INFERENCE_INFERENCE_ENGINE_H_

#include <memory>
#include <vector>
#include "traffic_perception/inference/imodel_backend.h"
#include "traffic_perception/inference/inference_sink.h"
#include "traffic_perception/ingestion/atomic_frame_buffer.h"
#include "traffic_perception/core/frame_pool.h"
#include "traffic_perception/core/config_manager.h"

namespace traffic_perception {

class InferenceEngine {
 public:
  // Constructor Injection:
  // - backend: AI model backend (ref)
  // - buffer: source of frames (reference)
  // - pool: // Used only to release frames when ownership
            // cannot be transferred successfully.
  // - sink: consumer of inference results (reference)
  // - configManager: configuration access
  InferenceEngine(IModelBackend& backend,
                  AtomicFrameBuffer& buffer,
                  FramePool& pool,
                  IInferenceSink& sink,
                  const std::array<Roi, NUM_LANES>& laneRois);

  // Processes all lanes in one cycle:
  // For each lane, take frame, run inference, transfer to sink.
  void runOneCycle();

 private:
  IModelBackend& backend_;
  AtomicFrameBuffer& buffer_;
  FramePool& pool_;
  IInferenceSink& sink_;
  std::array<Roi, NUM_LANES> laneRois_;
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_INFERENCE_INFERENCE_ENGINE_H_
