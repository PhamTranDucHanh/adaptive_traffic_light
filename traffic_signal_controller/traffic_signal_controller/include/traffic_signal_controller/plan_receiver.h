#ifndef TRAFFIC_SIGNAL_CONTROLLER_PLAN_RECEIVER_H_
#define TRAFFIC_SIGNAL_CONTROLLER_PLAN_RECEIVER_H_

#include <cstdint>

#include "common/config.h"
#include "common/plan_sync_channel.h"
#include "common/timing_plan.h"

class PlanReceiver final {
 public:
  explicit PlanReceiver(PlanSyncChannel& syncChannel);

  // Communication layer gọi hàm này khi nhận được plan mới.
  bool ReceivePlan(const TimingPlan& plan);

 private:
  bool ValidatePlan(const TimingPlan& plan) const;

  PlanData TranslatePlan(const TimingPlan& plan,
                         std::uint64_t receivedTimestampNs) const;

  static std::uint64_t GetMonotonicTimestampNs();

  PlanSyncChannel& syncChannel_;
};

#endif  // TRAFFIC_SIGNAL_CONTROLLER_PLAN_RECEIVER_H_