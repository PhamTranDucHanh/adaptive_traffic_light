#ifndef TRAFFIC_PERCEPTION_INFERENCE_INFERENCE_ENGINE_H_
#define TRAFFIC_PERCEPTION_INFERENCE_INFERENCE_ENGINE_H_

#include <memory>
#include <vector>
#include "traffic_perception/inference/imodel_backend.h"
#include "traffic_perception/inference/inference_sink.h"
#include "traffic_perception/ingestion/atomic_frame_buffer.h"
#include "traffic_perception/core/frame_pool.h"

namespace traffic_perception {

class InferenceEngine {
 public:
  // Constructor Injection:
  // - backend: AI model backend (owned)
  // - buffer: source of frames (reference)
  // - pool: // Used only to release frames when ownership
            // cannot be transferred successfully.
  // - sink: consumer of inference results (reference)
  InferenceEngine(std::unique_ptr<IModelBackend> backend,
                  AtomicFrameBuffer& buffer,
                  FramePool& pool,
                  IInferenceSink& sink);

  // Processes all lanes in one cycle:
  // For each lane, take frame, run inference, transfer to sink.
  void runOneCycle();

 private:
  std::unique_ptr<IModelBackend> backend_;
  AtomicFrameBuffer& buffer_;
  FramePool& pool_;
  IInferenceSink& sink_;
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_INFERENCE_INFERENCE_ENGINE_H_
