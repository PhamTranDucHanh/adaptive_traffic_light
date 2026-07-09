#include "traffic_signal_controller/plan_receiver.h"

void PlanReceiver::receivePlan(const PlanData&) {}

bool PlanReceiver::validatePlan() { return true; }

PlanData PlanReceiver::translatePlan() { return {}; }

void PlanReceiver::accept() {}

void PlanReceiver::reject() {}

PlanData PlanReceiver::forwardPlan() { return {}; }