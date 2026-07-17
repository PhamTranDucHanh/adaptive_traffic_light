#include "traffic_signal_controller/plan_receiver.h"
#include "score/mw/log/logger.h"
#include "common/logging_context.h"

namespace {
score::mw::log::Logger& logger =
    score::mw::log::CreateLogger(ctrl::logging::kCtxPlan, "Plan receiver");
}

PlanReceiver::PlanReceiver(PlanSyncChannel& syncChannel)
    : syncChannel_(syncChannel), validPlanResult(false) {}

void PlanReceiver::receivePlan(const PlanData& plan) {
    logger.LogInfo() << "Received timing plan";
    pendingPlan_ = plan;
}

bool PlanReceiver::validatePlan() {
    if (!validPlanResult) {
        logger.LogWarn() << "Plan validation failed";
    } else {
        logger.LogDebug() << "Plan validated successfully";
    }
    return validPlanResult;
}

void PlanReceiver::accept() {
    logger.LogInfo() << "Plan accepted";
}

void PlanReceiver::reject() {
    logger.LogWarn() << "Plan rejected";
}

PlanData PlanReceiver::forwardPlan() {
    PlanData translated = translatePlan();
    logger.LogDebug() << "Forwarding plan to FSM engine via sync channel";
    syncChannel_.PublishPlan(translated);
    return translated;
}