#ifndef SIGNAL_CONTROL_DEMO_APPLICATION_H
#define SIGNAL_CONTROL_DEMO_APPLICATION_H

#include <score/mw/lifecycle/application.h>

#include <cstdint>
#include <mqueue.h>

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
  traffic_ipc::QueueStatus receiveLatestPlan(
      traffic_ipc::TimingPlan& plan) noexcept;
  bool isNewerTransportMessage(std::uint64_t publisherInstanceId,
                               std::uint64_t sequenceNumber) noexcept;
  bool validatePlan(const traffic_ipc::TimingPlan& plan) const noexcept;
  void shutdown();

  mqd_t queueDescriptor_{static_cast<mqd_t>(-1)};
  common::PeriodicWait periodicWait_;
  traffic_ipc::TimingPlan lastValidPlan_{};
  std::uint64_t publisherInstanceId_{0U};
  std::uint64_t lastSequenceNumber_{0U};
  std::uint32_t consecutiveMisses_{0U};
  bool hasTransportPosition_{false};
  bool initialized_{false};
};

}  // namespace signal_control_demo

#endif  // SIGNAL_CONTROL_DEMO_APPLICATION_H
