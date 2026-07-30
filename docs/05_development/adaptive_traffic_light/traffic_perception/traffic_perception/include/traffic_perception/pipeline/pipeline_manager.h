#ifndef TRAFFIC_PERCEPTION_PIPELINE_PIPELINE_MANAGER_H_
#define TRAFFIC_PERCEPTION_PIPELINE_PIPELINE_MANAGER_H_

#include <array>
#include <atomic>
#include <chrono>
#include <memory>

#include "traffic_perception/core/config_manager.h"
#include "traffic_perception/core/frame_pool.h"
#include "traffic_perception/inference/imodel_backend.h"
#include "traffic_perception/inference/inference_engine.h"
#include "traffic_perception/pipeline/analyzer.h"
#include "traffic_perception/pipeline/snapshot_publisher.h"

namespace traffic_perception {

class PipelineManager {
 public:
  PipelineManager(IModelBackend& backend, AtomicFrameBuffer& buffer,
                  FramePool& pool, const std::array<Roi, NUM_LANES>& laneRois,
                  std::chrono::milliseconds period,
                  std::chrono::milliseconds phase);

  void run(std::chrono::steady_clock::time_point startTime);
  void stop();

  Analyzer& analyzer() { return analyzer_; }
  SnapshotPublisher& getPublisher() { return publisher_; }

 private:
  void runOneCycle(int64_t expectedWakeup, int64_t wakeupNs, int64_t pipelineBegin);

  std::atomic<bool> running_{false};
  std::chrono::milliseconds period_;
  std::chrono::milliseconds phase_;

  std::array<Roi, NUM_LANES> laneRois_{};

  Analyzer analyzer_;
  SnapshotPublisher publisher_;
  InferenceEngine engine_;
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_PIPELINE_PIPELINE_MANAGER_H_
