#include "traffic_perception/perception_module.h"

#include <iostream>

#include <opencv2/core.hpp>

namespace traffic_perception {

bool PerceptionModule::initModule(const std::string& configPath) {
  std::cout << "[PERCEPTION_MODULE][INIT] config=" << configPath << '\n';

  // TODO(domain): ConfigManager hiện chưa dùng configPath. Thêm API parse,
  // validate file và fail nếu model/stream/transport config không hợp lệ.
  if (!configManager_.loadConfig()) {
    return false;
  }
  const AppConfig config = configManager_.getConfig();

  std::int32_t streamId{0};
  for (auto& worker : workers_) {
    if (!worker.initStream(config, streamId++)) {
      return false;
    }
  }

  // TODO(domain): initialize model backend bằng config.ModelPath.
  analyzer_.initViewer(&viewer_);
  publisher_.initTelemetry(&telemetry_);
  publisher_.initSender(&snapshotSender_);

  if (!snapshotSender_.open()) {
    return false;
  }

  snapshotSenderOpen_ = true;
  telemetry_.TotalCycles = 0;
  nextFrameId_ = 0;
  nextLaneIndex_ = 0U;
  return true;
}

bool PerceptionModule::startThreads() {
  if (started_) {
    return true;
  }

  // TODO(domain): tạo worker threads, truyền từng queues_[lane], và lưu
  // stop/join handle. Không detach thread.
  started_ = true;
  return true;
}

bool PerceptionModule::processAndPublishOneSnapshot() {
  if (!started_ || !snapshotSenderOpen_) {
    return false;
  }

  constexpr std::int32_t kRows{480};
  constexpr std::int32_t kColumns{640};

  FrameContext context{};
  const std::size_t lane = nextLaneIndex_;

  if (engine_.consumeFromBuffer(lane, context)) {
    // Successfully got frame
  } else {
    // PoC-only input để teammate chạy smoke ngay.
    // TODO(domain): bỏ synthetic frame khi StreamWorker enqueue frame thật.
    context.CapturedFrame = new Frame(); // Using new temporarily until FramePool is fully integrated in StreamWorker
    context.CapturedFrame->FrameId = ++nextFrameId_;
    context.LaneId = static_cast<std::int32_t>(lane);
    context.CapturedFrame->Image = cv::Mat::zeros(kRows, kColumns, CV_8UC3);
  }

  // TODO(domain): inference, tracking, aggregation và stale-input policy thật.
  engine_.executeInference(context);
  analyzer_.trackAndAnalyze(context);

  // TODO(domain): map FrameContext → versioned TrafficSnapshot wire DTO.
  const bool published = publisher_.broadcastSnapshot(context);
  if (published) {
    ++telemetry_.TotalCycles;
  }

  nextLaneIndex_ = (lane + 1U) % buffers_.size();
  return published;
}

void PerceptionModule::stopThreads() {
  if (started_) {
    // TODO(domain): request stop rồi join tất cả worker threads theo thứ tự.
    started_ = false;
  }

  if (snapshotSenderOpen_) {
    snapshotSender_.close();
    snapshotSenderOpen_ = false;
  }
}

}  // namespace traffic_perception
