#ifndef TRAFFIC_PERCEPTION_TRAFFIC_PERCEPTION_APPLICATION_H_
#define TRAFFIC_PERCEPTION_TRAFFIC_PERCEPTION_APPLICATION_H_

#include <cstdint>

#include <score/mw/lifecycle/application.h>

#include "traffic_perception/lifecycle_health_reporter.h"

namespace traffic_perception {

class TrafficPerceptionApplication final
    : public score::mw::lifecycle::Application {
 public:
  std::int32_t Initialize(
      const score::mw::lifecycle::ApplicationContext& context) override;
  std::int32_t Run(
      const score::cpp::stop_token& stopToken) override;

 private:
  LifecycleHealthReporter healthReporter_{};
  std::uint64_t cycleCount_{0U};
  bool initialized_{false};
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_TRAFFIC_PERCEPTION_APPLICATION_H_
