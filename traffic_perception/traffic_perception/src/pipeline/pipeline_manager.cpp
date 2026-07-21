#include "traffic_perception/pipeline/pipeline_manager.h"

namespace traffic_perception {

PipelineManager::PipelineManager(std::unique_ptr<IModelBackend> backend,
                                 AtomicFrameBuffer& buffer,
                                 FramePool& pool,
                                 ConfigManager& configManager)
    : laneRois_(configManager.getConfig().laneRois),
      analyzer_(pool, laneRois_),
      publisher_(),
      // Inject internal analyzer into engine
      engine_(std::move(backend), buffer, pool, analyzer_, configManager) {}

void PipelineManager::runOneCycle() {
  engine_.runOneCycle();

  // Construct and publish snapshot
  TrafficSnapshot snapshot = analyzer_.buildTrafficSnapshot();
  publisher_.broadcastSnapshot(snapshot);
}

}  // namespace traffic_perception
