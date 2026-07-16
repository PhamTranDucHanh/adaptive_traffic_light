#ifndef TRAFFIC_TIMING_DECISION_TIMING_DECISION_APPLICATION_H
#define TRAFFIC_TIMING_DECISION_TIMING_DECISION_APPLICATION_H

#include <cstdint>

#include <score/mw/lifecycle/application.h>

#include "periodic_service.h"

namespace traffic_timing_decision {

class TimingDecisionApplication final
    : public score::mw::lifecycle::Application {
 public:
  std::int32_t Initialize(
      const score::mw::lifecycle::ApplicationContext& context) override;
  std::int32_t Run(const score::cpp::stop_token& stopToken) override;

 private:
  PeriodicService service_;
  std::uint64_t cycleCount_{0U};
  bool initialized_{false};
};

}  // namespace traffic_timing_decision

#endif  // TRAFFIC_TIMING_DECISION_TIMING_DECISION_APPLICATION_H
