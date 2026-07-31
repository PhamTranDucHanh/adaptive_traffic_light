#ifndef TRAFFIC_PERCEPTION_TRAFFIC_PERCEPTION_APPLICATION_H_
#define TRAFFIC_PERCEPTION_TRAFFIC_PERCEPTION_APPLICATION_H_

#include <chrono>
#include <cstdint>

#include <score/mw/lifecycle/application.h>

#include "traffic_perception/core/config_manager.h"
#include "traffic_perception/lifecycle_health_reporter.h"
#include "traffic_perception/perception_module.h"
#include "traffic_perception/viewer/opencv_lanes_viewer.h"

namespace traffic_perception {

class TrafficPerceptionApplication final
    : public score::mw::lifecycle::Application {
 public:
  std::int32_t Initialize(
      const score::mw::lifecycle::ApplicationContext& context) override;
  std::int32_t Run(
      const score::cpp::stop_token& stopToken) override;

 private:
  void shutdown();

  ConfigManager configManager_{"config/traffic_perception_config.json"};
  PerceptionModule perceptionModule_{};
  OpenCVLanesViewer viewer_{};
  LifecycleHealthReporter healthReporter_{};
  std::chrono::milliseconds viewerPeriod_{0};
  std::chrono::milliseconds viewerPhase_{0};
  std::uint64_t cycleCount_{0U};
  bool perceptionStarted_{false};
  bool viewerInitialized_{false};
  bool initialized_{false};
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_TRAFFIC_PERCEPTION_APPLICATION_H_
