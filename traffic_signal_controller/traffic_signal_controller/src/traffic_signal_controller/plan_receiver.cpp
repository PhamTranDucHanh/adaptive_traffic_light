#include "traffic_signal_controller/plan_receiver.h"

#include "common/logging_contexts.h"
#include "score/mw/log/logger.h"

namespace {

score::mw::log::Logger& Logger() {
  static score::mw::log::Logger& logger =
      score::mw::log::CreateLogger(
          ctrl::logging::kCtxPlan,
          "Plan receiver");
  return logger;
}

}


void PlanReceiver::receivePlan(const PlanData&) {}

bool PlanReceiver::validatePlan() { return true; }

PlanData PlanReceiver::translatePlan() { return {}; }

void PlanReceiver::accept() {}

void PlanReceiver::reject() {}

PlanData PlanReceiver::forwardPlan() { return {}; }