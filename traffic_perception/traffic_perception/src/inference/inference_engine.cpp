#include "traffic_perception/inference/inference_engine.h"
#include <iostream>

namespace traffic_perception {

InferenceEngine::InferenceEngine(std::unique_ptr<IModelBackend> backend,
                                 AtomicFrameBuffer& buffer,
                                 FramePool& pool,
                                 IInferenceSink& sink)
    : backend_(std::move(backend)),
      buffer_(buffer),
      pool_(pool),
      sink_(sink) {}

void InferenceEngine::runOneCycle() {
  for (uint32_t laneId = 0; laneId < NUM_LANES; ++laneId) {
    // 1. Ownership Transfer: Engine takes Frame from Buffer
    // Ownership transferred from AtomicFrameBuffer
    Frame* frame = buffer_.take(laneId);
    
    // 2. Skip if no frame
    if (frame == nullptr) {
      continue;
    }
    
    std::cout << "[InferenceEngine] Received FrameId: " << frame->FrameId << " Lane: " << laneId << std::endl;

    // 3. Perform Inference
    InferenceResult result = backend_->infer(*frame);
    result.LaneId = static_cast<int32_t>(laneId);
    std::cout << "[InferenceEngine] Inferenced FrameId: " << frame->FrameId << std::endl;
    
    // 4. Ownership Transfer: Handoff Frame + Result to Sink
    sink_.accept(frame, std::move(result));
    
    // Note: InferenceEngine no longer owns 'frame'. 
    // It MUST NOT release or access it further.
  }
}

}  // namespace traffic_perception
