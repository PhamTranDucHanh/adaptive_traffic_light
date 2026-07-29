#include <pthread.h>
#include <sched.h>

#include <memory>
#include <opencv2/opencv.hpp>
#include <string>
#include <thread>

#include "rules_cc/cc/runfiles/runfiles.h"
#include "score/mw/log/logging.h"
#include "traffic_perception/core/config_manager.h"
#include "traffic_perception/perception_module.h"
#include "traffic_perception/viewer/opencv_lanes_viewer.h"

using rules_cc::cc::runfiles::Runfiles;
using namespace traffic_perception;

int main(int argc, char* argv[]) {
  // Pin main thread/process to CPU Core 2
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(2, &cpuset);  // Core Index = 2
  pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

  std::string error;
  std::unique_ptr<Runfiles> runfiles(Runfiles::Create(argv[0], &error));

  if (!runfiles) {
    return 1;
  }

  //---------------------------------------------------------------------------
  // Load configuration
  //---------------------------------------------------------------------------

  std::string configPath =
      runfiles->Rlocation("_main/config/traffic_perception_config.json");

  if (configPath.empty()) {
    return 1;
  }

  ConfigManager configManager(configPath);

  if (!configManager.loadConfig()) {
    return 1;
  }

  AppConfig config = configManager.getConfig();

  config.modelPath = runfiles->Rlocation(config.modelPath);

  for (std::size_t i = 0; i < NUM_LANES; ++i) {
    config.lanes[i].videoSource =
        runfiles->Rlocation(config.lanes[i].videoSource);
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
    // Scheduled release for this cycle.
    const auto scheduledRelease = nextRelease;

    std::this_thread::sleep_until(scheduledRelease);

    const auto wakeup = std::chrono::steady_clock::now();

    const int64_t expectedWakeup =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            scheduledRelease.time_since_epoch())
            .count();

    const int64_t renderBegin =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            wakeup.time_since_epoch())
            .count();

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

    // If we missed more than one period, drop backlog and
    // restart the schedule from the current instant.
    if (wakeup > scheduledRelease + period) {
      nextRelease = wakeup + period;
    } else {
      nextRelease = scheduledRelease + period;
    }
  }

  //---------------------------------------------------------------------------
  // Shutdown
  //---------------------------------------------------------------------------

  viewer.shutdown();

  perception.stopThreads();

  return 0;
}
