#ifndef TRAFFIC_PERCEPTION_PERCEPTION_MODULE_H_
#define TRAFFIC_PERCEPTION_PERCEPTION_MODULE_H_

#include <pthread.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "traffic_perception/core/config_manager.h"
#include "traffic_perception/core/frame_pool.h"
#include "traffic_perception/core/types.h"
#include "traffic_perception/inference/imodel_backend.h"
#include "traffic_perception/ingestion/atomic_frame_buffer.h"
#include "traffic_perception/ingestion/stream_worker.h"
#include "traffic_perception/io/snapshot_sender.h"
#include "traffic_perception/pipeline/pipeline_manager.h"

namespace traffic_perception {

struct StreamThreadContext {
  StreamWorker* worker{nullptr};
  AtomicFrameBuffer* buffer{nullptr};
  std::chrono::steady_clock::time_point startTime;
};

struct PipelineThreadContext {
  PipelineManager* pipeline{nullptr};
  std::chrono::steady_clock::time_point startTime;
};

class PerceptionModule final {
 public:
  bool initModule(const AppConfig& config);
  bool startThreads();
  void stopThreads();
  Analyzer& analyzer();
  const Analyzer& analyzer() const;
  IModelBackend* backend();
  std::array<cv::Mat, NUM_LANES> latestPreviewFrames() const;
  std::chrono::steady_clock::time_point getStartTime();

 private:
  static bool ConfigureRealtimeThreadAttr(pthread_attr_t& attr,
                                          const ThreadConfig& config);

  std::chrono::steady_clock::time_point startTime_;

  FramePool pool_{};

  std::array<StreamWorker, NUM_LANES> workers_{};
  AtomicFrameBuffer buffer_{};
  std::array<StreamThreadContext, NUM_LANES> streamThreadContexts_{};
  PipelineThreadContext pipelineThreadContext_;

  std::unique_ptr<PipelineManager> pipelineManager_;
  std::unique_ptr<IModelBackend> backend_;

  MQSnapshotSender snapshotSender_{};

  std::array<pthread_t, NUM_LANES> streamThreads_{};
  pthread_t pipelineThread_{};

  std::array<pthread_attr_t, NUM_LANES> streamThreadAttrs_{};
  pthread_attr_t pipelineThreadAttr_{};

  AppConfig config_{};

  bool snapshotSenderOpen_{false};
  bool started_{false};
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_PERCEPTION_MODULE_H_
