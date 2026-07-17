#ifndef TRAFFIC_SIGNAL_CONTROLLER_CONTROLLER_PERIODIC_SERVICE_H_
#define TRAFFIC_SIGNAL_CONTROLLER_CONTROLLER_PERIODIC_SERVICE_H_

#include <chrono>

#include "traffic_signal_controller/controller_module.h"
#include "traffic_signal_controller/lifecycle_health_reporter.h"

namespace traffic_signal_controller {

class ControllerPeriodicService final {
 public:
  bool initialize();
  bool runCycle();
  bool shutdown();
  std::chrono::milliseconds period() const noexcept;

 private:
  ControllerModule module_{};
  LifecycleHealthReporter health_{};
  bool running_{false};
};

}  // namespace traffic_signal_controller

#endif  // TRAFFIC_SIGNAL_CONTROLLER_CONTROLLER_PERIODIC_SERVICE_