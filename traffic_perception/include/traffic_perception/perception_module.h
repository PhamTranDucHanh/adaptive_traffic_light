#ifndef TRAFFIC_PERCEPTION_PERCEPTION_MODULE_H
#define TRAFFIC_PERCEPTION_PERCEPTION_MODULE_H

#include <array>
#include <string>

#include "traffic_perception/core/config_manager.h"
#include "traffic_perception/inference/inference_engine.h"
#include "traffic_perception/ingestion/safe_frame_queue.h"
#include "traffic_perception/ingestion/stream_watchdog.h"
#include "traffic_perception/ingestion/stream_worker.h"
#include "traffic_perception/io/multi_lane_viewer.h"
#include "traffic_perception/io/snapshot_sender.h"
#include "traffic_perception/io/telemetry_manager.h"
#include "traffic_perception/pipeline/pipeline_node.h"
#include "traffic_perception/pipeline/snapshot_publisher.h"
#include "traffic_perception/pipeline/traffic_analyzer.h"

class PerceptionModule {
 private:
  ConfigManager configManager;
  StreamWatchdog watchdog;
  std::array<StreamWorker, 4> workers;
  std::array<SafeFrameQueue, 4> queues;
  InferenceEngine engine;
  TrafficAnalyzer analyzer;
  MultiLaneViewer viewer;
  SnapshotPublisher publisher;
  TelemetryManager telemetry;
  MQSnapshotSender snapshotSender;
  PipelineNode* pipelineHead;

 public:
  bool initModule(const std::string &configPath);
  void startThreads();
  void stopThreads();
};

#endif  // TRAFFIC_PERCEPTION_PERCEPTION_MODULE_H
