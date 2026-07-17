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
#include "traffic_perception/io/multi_lane_viewer.h"
#include "traffic_perception/io/snapshot_sender.h"
#include "traffic_perception/io/telemetry_manager.h"
#include "traffic_perception/pipeline/pipeline_node.h"
#include "traffic_perception/pipeline/snapshot_publisher.h"
#include "traffic_perception/pipeline/traffic_analyzer.h"

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
  std::array<StreamWorker, 4U> workers_{};
  std::array<AtomicFrameBuffer, 4U> buffers_{};
  InferenceEngine engine_{};
  TrafficAnalyzer analyzer_{};
  MultiLaneViewer viewer_{};
  SnapshotPublisher publisher_{};
  TelemetryManager telemetry_{};
  MQSnapshotSender snapshotSender_{};
  PipelineNode* pipelineHead_{nullptr};

  std::int32_t nextFrameId_{0};
  std::size_t nextLaneIndex_{0U};
  bool snapshotSenderOpen_{false};
  bool started_{false};
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_PERCEPTION_MODULE_H_
