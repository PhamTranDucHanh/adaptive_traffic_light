#include "traffic_perception/traffic_perception_application.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <opencv2/opencv.hpp>

#include "score/concurrency/interruptible_wait.h"
#include "score/mw/log/rust/stdout_logger_init.h"
#include "traffic_perception/ingestion/stream_worker.h"

namespace traffic_perception {

std::int32_t TrafficPerceptionApplication::Initialize(
    const score::mw::lifecycle::ApplicationContext& context) {
  (void)context;
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  score::mw::log::rust::StdoutLoggerBuilder loggerBuilder;
  loggerBuilder.Context("TPER")
      .LogLevel(score::mw::log::rust::LogLevel::Verbose)
      .SetAsDefaultLogger();

  if (!healthReporter_.initialize()) {
    std::cerr << "[TRAFFIC_PERCEPTION][INIT][ERROR] HealthReporter "
                 "initialization failed\n";
    return EXIT_FAILURE;
  }

  // Pipeline Initialization
  if (!pool_.init(20)) return EXIT_FAILURE;

  backend_ = std::make_unique<YOLOv8Backend>("test/data/yolov8n.onnx");
  backendPtr_ = backend_.get();
  pipeline_ = std::make_unique<PipelineManager>(std::move(backend_), buffer_, pool_);

  for (uint32_t i = 0; i < 4; ++i) {
      auto worker = std::make_unique<StreamWorker>();
      worker->initStream("test/data/traffic.mp4", i, &pool_, std::chrono::milliseconds(200));
      worker->start(buffer_);
      workers_.push_back(std::move(worker));
  }

  cv::namedWindow("Pipeline Integration", cv::WINDOW_AUTOSIZE);

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

  using namespace std::chrono_literals;
  constexpr auto kPeriod = 100ms;
  auto nextRelease = std::chrono::steady_clock::now() + kPeriod;
  std::int32_t exitCode{EXIT_SUCCESS};

  std::array<cv::Mat, 4> lastRenderedFrames;
  for (auto& frame : lastRenderedFrames) frame = cv::Mat();

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

    // Pipeline Logic
    pipeline_->runOneCycle();
    auto frames = pipeline_->analyzer().takeRenderFrames();

    std::vector<cv::Mat> canvasLanes(4);
    for (uint32_t i = 0; i < 4; ++i) {
        if (frames[i] && !frames[i]->Image.empty()) {
            backendPtr_->draw(frames[i]->Image, pipeline_->analyzer().latestResult(i));
            cv::resize(frames[i]->Image, lastRenderedFrames[i], cv::Size(320, 240));
            pipeline_->analyzer().releaseFrame(frames[i]);
        }
        
        if (!lastRenderedFrames[i].empty()) {
            canvasLanes[i] = lastRenderedFrames[i];
        } else {
            canvasLanes[i] = cv::Mat(240, 320, CV_8UC3, cv::Scalar(0, 0, 0));
            cv::putText(canvasLanes[i], "Waiting...", cv::Point(100, 120),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
        }
    }
    
    cv::Mat top, bottom, canvas;
    cv::hconcat(canvasLanes[0], canvasLanes[1], top);
    cv::hconcat(canvasLanes[2], canvasLanes[3], bottom);
    cv::vconcat(top, bottom, canvas);
    cv::imshow("Pipeline Integration", canvas);
    cv::waitKey(1);

    ++cycleCount_;
    healthReporter_.finishPerceptionCycle();
    nextRelease += kPeriod;
  }

  // Shutdown
  for (auto& worker : workers_) worker->stop();
  cv::destroyAllWindows();
  
  initialized_ = false;
  std::cout << "[TRAFFIC_PERCEPTION][STOP] cycles_completed=" << cycleCount_ << '\n';
  return exitCode;
}

}  // namespace traffic_perception
