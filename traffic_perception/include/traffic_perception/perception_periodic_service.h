#ifndef TRAFFIC_PERCEPTION_PERCEPTION_PERIODIC_SERVICE_H_
#define TRAFFIC_PERCEPTION_PERCEPTION_PERIODIC_SERVICE_H_

#include <chrono>
#include <string>

#include "traffic_perception/lifecycle_health_reporter.h"
#include "traffic_perception/perception_module.h"

namespace traffic_perception {

class PerceptionPeriodicService final {
 public:
  bool initialize(const std::string& configPath);
  bool runCycle();
  void shutdown();
  std::chrono::milliseconds period() const noexcept;

 private:
  ::PerceptionModule module_{};
  LifecycleHealthReporter health_{};
  bool running_{false};
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_PERCEPTION_PERIODIC_SERVICE_H_
