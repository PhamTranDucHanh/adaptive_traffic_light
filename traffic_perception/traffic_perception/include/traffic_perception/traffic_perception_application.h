#ifndef TRAFFIC_PERCEPTION_TRAFFIC_PERCEPTION_APPLICATION_H_
#define TRAFFIC_PERCEPTION_TRAFFIC_PERCEPTION_APPLICATION_H_

#include <cstdint>
#include <memory>
#include <vector>
#include <opencv2/opencv.hpp>

#include <score/mw/lifecycle/application.h>

#include "traffic_perception/lifecycle_health_reporter.h"
#include "traffic_perception/core/frame_pool.h"
#include "traffic_perception/ingestion/atomic_frame_buffer.h"
#include "traffic_perception/ingestion/stream_worker.h"
#include "traffic_perception/inference/yolov8_backend.h"
#include "traffic_perception/pipeline/pipeline_manager.h"

namespace traffic_perception {

class TrafficPerceptionApplication final
    : public score::mw::lifecycle::Application {
 public:
  std::int32_t Initialize(
      const score::mw::lifecycle::ApplicationContext& context) override;
  std::int32_t Run(
      const score::cpp::stop_token& stopToken) override;

 private:
  LifecycleHealthReporter healthReporter_{};
  std::uint64_t cycleCount_{0U};
  bool initialized_{false};

  // Pipeline members
  FramePool pool_;
  AtomicFrameBuffer buffer_;
  std::vector<std::unique_ptr<StreamWorker>> workers_;
  std::unique_ptr<YOLOv8Backend> backend_;
  YOLOv8Backend* backendPtr_{nullptr};
  std::unique_ptr<PipelineManager> pipeline_;
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_TRAFFIC_PERCEPTION_APPLICATION_H_
