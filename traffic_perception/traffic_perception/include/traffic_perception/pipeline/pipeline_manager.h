#ifndef TRAFFIC_PERCEPTION_PIPELINE_PIPELINE_MANAGER_H_
#define TRAFFIC_PERCEPTION_PIPELINE_PIPELINE_MANAGER_H_

#include <array>
#include <memory>
#include "traffic_perception/inference/inference_engine.h"
#include "traffic_perception/pipeline/analyzer.h"
#include "traffic_perception/pipeline/snapshot_publisher.h"
#include "traffic_perception/inference/imodel_backend.h"
#include "traffic_perception/core/frame_pool.h"
#include "traffic_perception/core/config_manager.h"

namespace traffic_perception {

class PipelineManager {
 public:
  PipelineManager(std::unique_ptr<IModelBackend> backend,
                  AtomicFrameBuffer& buffer,
                  FramePool& pool,
                  const std::array<Roi, NUM_LANES>& laneRois);

  void runOneCycle();
  Analyzer& analyzer() { return analyzer_; }
  SnapshotPublisher& getPublisher() { return publisher_; }

 private:
  std::array<Roi, NUM_LANES> laneRois_{};
  Analyzer analyzer_;
  SnapshotPublisher publisher_;
  InferenceEngine engine_;
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_PIPELINE_PIPELINE_MANAGER_H_
