#include "traffic_perception/traffic_perception_application.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <opencv2/opencv.hpp>

#include "score/concurrency/interruptible_wait.h"
#include "score/mw/log/rust/stdout_logger_init.h"
#include "traffic_perception/ingestion/stream_worker.h"
#include "traffic_perception/inference/yolov8_backend.h"

namespace traffic_perception {

#include "tools/cpp/runfiles/runfiles.h"
// ... existing includes ...

std::int32_t TrafficPerceptionApplication::Initialize(
    const score::mw::lifecycle::ApplicationContext& context) {
  (void)context;

  std::string error;
  std::unique_ptr<bazel::tools::cpp::runfiles::Runfiles> runfiles(
      bazel::tools::cpp::runfiles::Runfiles::Create(nullptr, &error));
  if (!runfiles) return EXIT_FAILURE;

  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  // ... (logger setup) ...

  if (!healthReporter_.initialize()) {
    return EXIT_FAILURE;
  }

  // Resolve resources
  std::string modelPath = runfiles->Rlocation("traffic_perception/test/data/yolov8n.onnx");
  std::array<std::string, NUM_LANES> videoPaths = {
      runfiles->Rlocation("traffic_perception/test/data/traffic.mp4"),
      runfiles->Rlocation("traffic_perception/test/data/traffic2.mp4"),
      runfiles->Rlocation("traffic_perception/test/data/traffic3.mp4"),
      runfiles->Rlocation("traffic_perception/test/data/traffic4.mp4")
  };

  // Pipeline Initialization
  if (!configManager_.loadConfig(modelPath, videoPaths)) return EXIT_FAILURE;
  AppConfig config = configManager_.getConfig();

  if (!pool_.init(20)) return EXIT_FAILURE;

  // ... (rest of init)
  for (size_t i = 0; i < NUM_LANES; ++i) {
      laneRois[i] = config.lanes[i].roi;
  }

  if (!pool_.init(20)) return EXIT_FAILURE;

  backend_ = std::make_unique<YOLOv8Backend>("test/data/yolov8n.onnx");
  backendPtr_ = backend_.get();
  pipeline_ = std::make_unique<PipelineManager>(std::move(backend_), buffer_, pool_, laneRois);

  // MQ Sender Setup
  mqSender_ = std::make_unique<MQSnapshotSender>();
  if (mqSender_->open()) {
      pipeline_->getPublisher().initSender(mqSender_.get());
  } else {
      std::cerr << "[TRAFFIC_PERCEPTION][INIT][ERROR] Failed to open snapshot sender\n";
      return EXIT_FAILURE;
  }

  // Visualization Setup
  viewer_.init(config, backendPtr_);

  // Start Workers
  for (uint32_t i = 0; i < NUM_LANES; ++i) {
      auto worker = std::make_unique<StreamWorker>();
      worker->initStream(config.lanes[i].videoSource, i, &pool_, config.CapturePeriod);
      worker->start(buffer_);
      workers_.push_back(std::move(worker));
  }

  cycleCount_ = 0U;
  initialized_ = true;
  std::cout << "[TRAFFIC_PERCEPTION][INIT] application ready\n";
  return EXIT_SUCCESS;
}

std::int32_t TrafficPerceptionApplication::Run(
    const score::cpp::stop_token& stopToken) {
  if (!initialized_) {
    return EXIT_FAILURE;
  }

  AppConfig config = configManager_.getConfig();
  auto nextRelease = std::chrono::steady_clock::now() + config.PipelinePeriod;
  std::int32_t exitCode{EXIT_SUCCESS};

  std::cout << "[TRAFFIC_PERCEPTION][RUN] periodic loop started\n";
  while (!stopToken.stop_requested()) {
    if (score::concurrency::wait_until(stopToken, nextRelease)) {
      break;
    }

    if (!healthReporter_.startPerceptionCycle()) {
      std::cerr << "[TRAFFIC_PERCEPTION][RUN][ERROR] could not start health-monitored perception cycle\n";
      exitCode = EXIT_FAILURE;
      break;
    }

    // Orchestrated Pipeline Logic (Reusing pipeline_manager_test flow)
    pipeline_->runOneCycle();
    viewer_.render(pipeline_->analyzer());

    // UI Handle
    if (cv::waitKey(1) == 'q') {
        exitCode = EXIT_SUCCESS; // Signal handled by stop_token in production
        break;
    }

    ++cycleCount_;
    healthReporter_.finishPerceptionCycle();
    nextRelease += config.PipelinePeriod;
  }

  // Shutdown (Matching pipeline_manager_test)
  viewer_.shutdown();
  for (auto& worker : workers_) worker->stop();
  mqSender_->close();
  
  initialized_ = false;
  std::cout << "[TRAFFIC_PERCEPTION][STOP] cycles_completed=" << cycleCount_ << '\n';
  return exitCode;
}

}  // namespace traffic_perception
