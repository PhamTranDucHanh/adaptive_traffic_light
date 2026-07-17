#ifndef TRAFFIC_SIGNAL_CONTROLLER_APPLICATION_H_
#define TRAFFIC_SIGNAL_CONTROLLER_APPLICATION_H_

#include <cstdint>

#include <score/mw/lifecycle/application.h>

#include "traffic_signal_controller/controller_periodic_service.h"

namespace traffic_signal_controller {

class TrafficSignalControllerApplication final
    : public score::mw::lifecycle::Application {
 public:
  std::int32_t Initialize(
      const score::mw::lifecycle::ApplicationContext& context) override;
  std::int32_t Run(
      const score::cpp::stop_token& stopToken) override;

 private:
  ControllerPeriodicService service_{};
  bool initialized_{false};
};

}  // namespace traffic_signal_controller

#endif  // TRAFFIC_SIGNAL_CONTROLLER_APPLICATION_H_