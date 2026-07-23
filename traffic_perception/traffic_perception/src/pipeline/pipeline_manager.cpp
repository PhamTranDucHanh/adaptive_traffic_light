#include "traffic_perception/pipeline/pipeline_manager.h"

namespace traffic_perception {

PipelineManager::PipelineManager(std::unique_ptr<IModelBackend> backend,
                                 AtomicFrameBuffer& buffer,
                                 FramePool& pool,
                                 const std::array<Roi, NUM_LANES>& laneRois)
    : laneRois_(laneRois),
      analyzer_(pool, laneRois_),
      publisher_(),
      // Inject internal analyzer into engine
      engine_(std::move(backend), buffer, pool, analyzer_, laneRois_) {}

void PipelineManager::runOneCycle() {
  engine_.runOneCycle();

  // Construct and publish snapshot
  TrafficSnapshot snapshot = analyzer_.buildTrafficSnapshot();
  
  FrameContext ctx;
  ctx.frame = analyzer_.latestFrame();
  ctx.timeline = analyzer_.latestTimeline();
  ctx.snapshot = snapshot;
  
  publisher_.broadcastSnapshot(ctx);
}

}  // namespace traffic_perception
