#ifndef PERCEPTION_DEMO_APPLICATION_H
#define PERCEPTION_DEMO_APPLICATION_H

#include <cstdint>

#include <score/mw/lifecycle/application.h>

#include "common/periodic_health_reporter.h"
#include "traffic_ipc/latest_value_queue.h"
#include "traffic_ipc/messages.h"

namespace perception_demo {

class PerceptionApplication final : public score::mw::lifecycle::Application {
 public:
  PerceptionApplication();

  std::int32_t Initialize(
      const score::mw::lifecycle::ApplicationContext& context) override;
  std::int32_t Run(const score::cpp::stop_token& stopToken) override;

 private:
  traffic_ipc::TrafficSnapshot makeSnapshot();
  void shutdown();

  traffic_ipc::LatestValuePublisher<traffic_ipc::TrafficSnapshot> publisher_;
  common::PeriodicHealthReporter healthReporter_;
  std::uint64_t nextFrameId_{1U};
  bool initialized_{false};
};

}  // namespace perception_demo

#endif  // PERCEPTION_DEMO_APPLICATION_H
