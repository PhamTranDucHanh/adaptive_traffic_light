#ifndef SIGNAL_CONTROL_DEMO_APPLICATION_H
#define SIGNAL_CONTROL_DEMO_APPLICATION_H

#include <score/mw/lifecycle/application.h>

#include <cstdint>

#include "common.h"
#include "traffic_ipc/latest_value_queue.h"
#include "traffic_ipc/messages.h"

#define SIGNAL_CONTROLLER_CONSUMED

namespace signal_control_demo {

class SignalControlApplication final
    : public score::mw::lifecycle::Application {
 public:
  SignalControlApplication();

  std::int32_t Initialize(
      const score::mw::lifecycle::ApplicationContext& context) override;
  std::int32_t Run(const score::cpp::stop_token& stopToken) override;

 private:
  bool validatePlan(const traffic_ipc::TimingPlan& plan) const noexcept;
  void shutdown();

  traffic_ipc::LatestValueConsumer<traffic_ipc::TimingPlan> consumer_;
  common::PeriodicWait periodicWait_;
  traffic_ipc::TimingPlan lastValidPlan_{};
  std::uint32_t consecutiveMisses_{0U};
  bool initialized_{false};
};

}  // namespace signal_control_demo

#endif  // SIGNAL_CONTROL_DEMO_APPLICATION_H
