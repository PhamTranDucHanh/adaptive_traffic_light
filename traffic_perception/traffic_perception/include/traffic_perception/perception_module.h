#ifndef TRAFFIC_PERCEPTION_PERCEPTION_MODULE_H_
#define TRAFFIC_PERCEPTION_PERCEPTION_MODULE_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "traffic_perception/core/config_manager.h"
#include "traffic_perception/inference/inference_engine.h"
#include "traffic_perception/ingestion/atomic_frame_buffer.h"
#include "traffic_perception/ingestion/stream_watchdog.h"
#include "traffic_perception/ingestion/stream_worker.h"
#include "traffic_perception/io/snapshot_sender.h"
#include "traffic_perception/io/telemetry_manager.h"
#include "traffic_perception/pipeline/pipeline_manager.h"

namespace traffic_perception {

class PerceptionModule final {
 public:
  bool initModule(const std::string& configPath);
  bool startThreads();
  bool processAndPublishOneSnapshot();
  void stopThreads();

 private:
  ConfigManager configManager_{};
  StreamWatchdog watchdog_{};
  FramePool pool_{};
  std::array<StreamWorker, 4U> workers_{};
  std::array<AtomicFrameBuffer, 4U> buffers_{};
  std::unique_ptr<PipelineManager> pipelineManager_;
  TelemetryManager telemetry_{};
  MQSnapshotSender snapshotSender_{};

  std::int32_t nextFrameId_{0};
  std::size_t nextLaneIndex_{0U};
  bool snapshotSenderOpen_{false};
  bool started_{false};
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_PERCEPTION_MODULE_H_
