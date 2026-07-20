#ifndef TRAFFIC_SIGNAL_CONTROLLER_SIGNAL_CONTROL_APP_H
#define TRAFFIC_SIGNAL_CONTROLLER_SIGNAL_CONTROL_APP_H

#include <cstdint>

#include <score/mw/lifecycle/application.h>

#include "common/health_reporter.h"

namespace traffic_signal_controller {

class SignalControlApplication final
    : public score::mw::lifecycle::Application {
 public:
  std::int32_t Initialize(
      const score::mw::lifecycle::ApplicationContext& context) override;
  std::int32_t Run(const score::cpp::stop_token& stopToken) override;

 private:
  HealthReporter healthReporter_;
  std::uint64_t cycleCount_{0U};
  bool initialized_{false};
};

}  // namespace traffic_signal_controller

#endif  // TRAFFIC_SIGNAL_CONTROLLER_SIGNAL_CONTROL_APP_H
