#ifndef PLAN_RECEIVER_H
#define PLAN_RECEIVER_H

#include <common/config.h>
#include <common/plan_sync_channel.h>

class PlanReceiver {
 public:
  explicit PlanReceiver(PlanSyncChannel& syncChannel);

  void receivePlan(const PlanData& plan);
  bool validatePlan();
  void accept();
  void reject();
  PlanData forwardPlan();

 private:
  PlanSyncChannel& syncChannel_;
  bool validPlanResult;
  PlanData pendingPlan_;
  PlanData translatePlan();
};

#endif