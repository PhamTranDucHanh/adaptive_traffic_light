#include <pthread.h>
#include <sched.h>

#include <filesystem>
#include <memory>
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>

#include "score/mw/log/logging.h"
#include "traffic_perception/core/config_manager.h"
#include "traffic_perception/perception_module.h"
#include "traffic_perception/viewer/opencv_lanes_viewer.h"
#include "traffic_perception/core/time_utils.h"

using namespace traffic_perception;

namespace {

std::filesystem::path parseResourcePath(int argc, char* argv[]) {
  for (int i = 1; i < argc - 1; ++i) {
    if (std::string(argv[i]) == "--resource_path") {
      return std::filesystem::path(argv[i + 1]);
    }
  }
  // Default: current working directory
  return std::filesystem::current_path();
}

}  // namespace


int main(int argc, char* argv[]) {
  // Pin main thread/process to CPU Core 2
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(2, &cpuset);  // Core Index = 2
  pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

  const std::filesystem::path resourcePath = parseResourcePath(argc, argv);

  //---------------------------------------------------------------------------
  // Load configuration
  //---------------------------------------------------------------------------

  const std::string configPath =
      (resourcePath / "config" / "traffic_perception_config.json").string();

  ConfigManager configManager(configPath);

  if (!configManager.loadConfig()) {
    score::mw::log::LogError() << "Failed to load config from: " << configPath;
    return 1;
  }

  AppConfig config = configManager.getConfig();

  // Resolve model and video paths relative to resourcePath.
  // Config stores relative paths like "test/data/model.onnx" — join directly.
  config.modelPath = (resourcePath / config.modelPath).string();

  for (std::size_t i = 0; i < NUM_LANES; ++i) {
    config.lanes[i].videoSource =
        (resourcePath / config.lanes[i].videoSource).string();
  }

  //---------------------------------------------------------------------------
  // Perception module
  //---------------------------------------------------------------------------

  PerceptionModule perception;

  if (!perception.initModule(config)) {
    score::mw::log::LogError() << "Failed to initialize perception module";
    return 1;
  }

  if (!perception.startThreads()) {
    score::mw::log::LogError() << "Failed to start perception threads";
    return 1;
  }

  //---------------------------------------------------------------------------
  // Viewer (main thread)
  //---------------------------------------------------------------------------

  OpenCVLanesViewer viewer;
  viewer.init(config, perception.backend());

  bool running = true;

  auto nextRelease = std::chrono::steady_clock::now() +
                     std::chrono::milliseconds(config.ViewerPhase);
  const auto period = std::chrono::milliseconds(config.ViewerPeriod);

  while (running) {
    const auto nowNs = GetMonotonicTimeNs();
    const int64_t periodNs = period.count() * 1000000LL;

    int64_t nextReleaseNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            nextRelease.time_since_epoch()).count();

    while (nextReleaseNs + periodNs <= nowNs) {
      nextReleaseNs += periodNs;
    }

    const int64_t expectedWakeup = nextReleaseNs;
    SleepUntilNs(expectedWakeup);

    const int64_t renderBegin = GetMonotonicTimeNs();

    viewer.render(perception.analyzer(), expectedWakeup, renderBegin);

    const int key = cv::waitKey(1);

    switch (key) {
      case 'q':
      case 27:
        running = false;
        break;

      default:
        break;
    }

    nextRelease = std::chrono::steady_clock::time_point(
        std::chrono::nanoseconds(nextReleaseNs)) + period;
  }

  //---------------------------------------------------------------------------
  // Shutdown
  //---------------------------------------------------------------------------

  viewer.shutdown();

  perception.stopThreads();

  return 0;
}
