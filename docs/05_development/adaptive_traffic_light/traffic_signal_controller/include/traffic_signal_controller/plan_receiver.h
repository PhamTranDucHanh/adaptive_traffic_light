#ifndef TRAFFIC_SIGNAL_CONTROLLER_PLAN_RECEIVER_H_
#define TRAFFIC_SIGNAL_CONTROLLER_PLAN_RECEIVER_H_

#include "common/config.h"
#include "common/plan_sync_channel.h"

class PlanReceiver final {
 public:
  explicit PlanReceiver(PlanSyncChannel& syncChannel);

  // Communication layer gọi hàm này khi nhận được plan mới.
  bool ReceivePlan(const TimingPlan& plan);

 private:
  bool ValidatePlan(const TimingPlan& plan) const;

  PlanData TranslatePlan(const TimingPlan& plan) const;

  PlanSyncChannel& syncChannel_;
};

#endif  // TRAFFIC_SIGNAL_CONTROLLER_PLAN_RECEIVER_H_
