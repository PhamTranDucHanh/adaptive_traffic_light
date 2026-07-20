#ifndef PLAN_RECEIVER_H
#define PLAN_RECEIVER_H

#include <common/config.h>

class PlanReceiver {
 public:
  void receivePlan(const PlanData& plan);

  bool validatePlan();

  void accept();

  void reject();

  PlanData forwardPlan();

 private:
  bool validPlanResult;

  PlanData translatePlan();
};

#endif