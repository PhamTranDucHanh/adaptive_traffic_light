#include "traffic_perception/traffic_perception_application.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <filesystem>

#include "score/concurrency/interruptible_wait.h"
#include "score/mw/log/rust/stdout_logger_init.h"

#include "traffic_perception/inference/yolov8_oiv7_backend.h"
#include <traffic_perception/inference/yolov8_backend.h>
#include "traffic_perception/core/types.h"
#include "traffic_perception/core/runtime_paths.h"
using namespace traffic_perception;

namespace traffic_perception {

std::int32_t TrafficPerceptionApplication::Initialize(
    const score::mw::lifecycle::ApplicationContext& context) {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  // HealthMonitor logs through the S-CORE Rust logger. Initialize it before
  // starting HealthReporter's asynchronous worker, as in cpp_supervised_app.
  score::mw::log::rust::StdoutLoggerBuilder loggerBuilder;
  loggerBuilder.Context("TPER")
      .LogLevel(score::mw::log::rust::LogLevel::Verbose)
      .SetAsDefaultLogger();

  // Load config
  std::string configPath = (RuntimePaths::Etc() / "traffic_perception_config.json").string();

  configManager_ = ConfigManager(configPath);

  if (!configManager_.loadConfig())
      return EXIT_FAILURE;

  AppConfig& config = configManager_.getConfig();
  
  // Resolve paths using RuntimePaths
  config.modelPath = (RuntimePaths::Models() / std::filesystem::path(config.modelPath).filename()).string();

  for (size_t i = 0; i < NUM_LANES; ++i)
      config.lanes[i].videoSource = (RuntimePaths::Etc() / std::filesystem::path(config.lanes[i].videoSource).filename()).string();
  
  // Build ROI array
  std::array<Roi, NUM_LANES> laneRois;

  for (size_t i = 0; i < NUM_LANES; ++i)
      laneRois[i] = config.lanes[i].roi;
  // Init FramePool
  if (!pool_.init(20))
      return EXIT_FAILURE;
  // Create backend
  backend_ =
      std::make_unique<YoloV8OIV7Backend>(config.modelPath);

  backendPtr_ = backend_.get();
  // Create PipelineManager
  pipeline_ =
      std::make_unique<PipelineManager>(
          std::move(backend_),
          buffer_,
          pool_,
          laneRois);
  // MQ Sender
  mqSender_ = std::make_unique<MQSnapshotSender>();

  if (!mqSender_->open())
      return EXIT_FAILURE;

  pipeline_->getPublisher().initSender(mqSender_.get());
  // Viewer
  viewer_.init(config, backendPtr_);
  // Workers
  for (uint32_t i = 0; i < NUM_LANES; ++i)
  {
      auto worker = std::make_unique<StreamWorker>();

      worker->initStream(
          config.lanes[i].videoSource,
          i,
          &pool_,
          config.CapturePeriod);

      worker->start(buffer_);

      workers_.push_back(std::move(worker));
  }

  if (!healthReporter_.initialize()) {
    std::cerr << "[TRAFFIC_PERCEPTION][INIT][ERROR] HealthReporter "
                 "initialization failed\n";
    return EXIT_FAILURE;
  }

  cycleCount_ = 0U;
  initialized_ = true;
  std::cout << "[TRAFFIC_PERCEPTION][INIT] application ready; "
               "period_ms=3000\n";
  return EXIT_SUCCESS;
}

std::int32_t TrafficPerceptionApplication::Run(
    const score::cpp::stop_token& stopToken) {
  if (!initialized_) {
    return EXIT_FAILURE;
  }

  using namespace std::chrono_literals;
  constexpr auto kPeriod = 3s;
  auto nextRelease = std::chrono::steady_clock::now() + kPeriod;
  std::int32_t exitCode{EXIT_SUCCESS};

  std::cout << "[TRAFFIC_PERCEPTION][RUN] periodic loop started\n";
  while (!stopToken.stop_requested()) {
    // The first release occurs after one complete period, giving the heartbeat
    // monitor a real 3-second baseline. Absolute releases avoid timer drift.
    if (score::concurrency::wait_until(stopToken, nextRelease)) {
      break;
    }

    if (!healthReporter_.startPerceptionCycle()) {
      std::cerr << "[TRAFFIC_PERCEPTION][RUN][ERROR] could not start "
                   "health-monitored perception cycle\n";
      exitCode = EXIT_FAILURE;
      break;
    }

    pipeline_->runOneCycle();

    viewer_.render(pipeline_->analyzer());

    if (cv::waitKey(1) == 'd')
        pipeline_->getPublisher().flush();

    ++cycleCount_;
    std::cout << "[TRAFFIC_PERCEPTION][CYCLE] hello; counter=" << cycleCount_
              << "; period_ms=3000\n";

    healthReporter_.finishPerceptionCycle();
    nextRelease += kPeriod;
  }

  viewer_.shutdown();
  for (auto& worker : workers_)
    worker->stop();

  if (mqSender_)
    mqSender_->close();

  healthReporter_.shutdown();
  initialized_ = false;
  std::cout << "[TRAFFIC_PERCEPTION][STOP] cycles_completed=" << cycleCount_
            << '\n';
  return exitCode;
}

}  // namespace traffic_perception
