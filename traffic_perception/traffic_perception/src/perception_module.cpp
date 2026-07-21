#include "traffic_perception/perception_module.h"

#include <iostream>
#include <opencv2/core.hpp>
#include "traffic_perception/inference/yolov8_backend.h"

namespace traffic_perception {

bool PerceptionModule::initModule(const std::string& configPath) {
  std::cout << "[PERCEPTION_MODULE][INIT] config=" << configPath << '\n';

  if (!configManager_.loadConfig()) {
    return false;
  }
  const AppConfig config = configManager_.getConfig();
  if (!pool_.init(20)) return false;

  // Create backend
  auto backend = std::make_unique<YOLOv8Backend>("test/data/yolov8m-oiv7.onnx");

  // PipelineManager will own the pipeline stages internally.
  pipelineManager_ = std::make_unique<PipelineManager>(std::move(backend), buffers_[0], pool_, configManager_);

  for (std::size_t i = 0; i < workers_.size(); ++i) {
    workers_.at(i).initStream(config.trafficVidSources.at(i), static_cast<std::int32_t>(i), &pool_);
  }

  snapshotSenderOpen_ = true; // Temporary
  return true;
}

bool PerceptionModule::startThreads() {
  if (started_) return true;

  started_ = true;
  return true;
}

void PerceptionModule::stopThreads() {
  if (started_) {
    started_ = false;
  }

  if (snapshotSenderOpen_) {
    snapshotSender_.close();
    snapshotSenderOpen_ = false;
  }
}

}  // namespace traffic_perception
