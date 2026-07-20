#ifndef TRAFFIC_PERCEPTION_PIPELINE_PIPELINE_MANAGER_H_
#define TRAFFIC_PERCEPTION_PIPELINE_PIPELINE_MANAGER_H_

#include <memory>
#include "traffic_perception/inference/inference_engine.h"
#include "traffic_perception/pipeline/analyzer.h"
#include "traffic_perception/pipeline/publisher.h"
#include "traffic_perception/inference/imodel_backend.h"
#include "traffic_perception/core/frame_pool.h"

namespace traffic_perception {

class PipelineManager {
 public:
  PipelineManager(std::unique_ptr<IModelBackend> backend,
                  AtomicFrameBuffer& buffer,
                  FramePool& pool);

  void runOneCycle();
  Analyzer& analyzer() { return analyzer_; }

 private:
  Analyzer analyzer_;
  Publisher publisher_;
  InferenceEngine engine_;
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_PIPELINE_PIPELINE_MANAGER_H_
