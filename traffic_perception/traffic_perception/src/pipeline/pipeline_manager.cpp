#include "traffic_perception/pipeline/pipeline_manager.h"

namespace traffic_perception {

PipelineManager::PipelineManager(std::unique_ptr<IModelBackend> backend,
                                 AtomicFrameBuffer& buffer,
                                 FramePool& pool)
    : analyzer_(pool),
      publisher_(),
      // Inject internal analyzer into engine
      engine_(std::move(backend), buffer, pool, analyzer_) {}

void PipelineManager::runOneCycle() {
  engine_.runOneCycle();
}

}  // namespace traffic_perception
