#include "traffic_signal_controller/signal_fsm_engine.h"

#include "common/logging_context.h"

namespace {
score::mw::log::Logger& logger =
    score::mw::log::CreateLogger(ctrl::logging::kCtxFsm, "Signal FSM engine");

constexpr std::string_view PhaseName(PhaseId id) {
    switch (id) {
        case PhaseId::NS_GREEN: return "NS_GREEN";
        case PhaseId::YELLOW:   return "YELLOW";
        case PhaseId::ALL_RED:  return "ALL_RED";
        case PhaseId::EW_GREEN: return "EW_GREEN";
    }
    return "UNKNOWN";
}
}  // namespace

SignalFSMEngine::SignalFSMEngine(PlanSyncChannel& syncChannel)
    : syncChannel_(syncChannel),
      remainingTimeMs(0),
      currentPhaseIndex(0),
      isEmergencyNS(false),
      isEmergencyEW(false),
      hasNewPlan(false) {
  clock_gettime(CLOCK_MONOTONIC, &deadline_);
  logger.LogInfo() << "FSM engine initialized, initial deadline set";
}

SignalDisplay SignalFSMEngine::processTick() {
  deadline_.tv_sec += 1;  // TODO: xác nhận 1s/tick hay theo TIMER_INTERVAL_MS
  fsm();
  return SignalDisplay{currentPlan.phases[currentPhaseIndex].phaseId,
                       remainingTimeMs};
}

void SignalFSMEngine::fsm() {
  if (currentPlan.phaseCount == 0) {
    logger.LogWarn() << "fsm() called with no active plan, skipping tick";
    return;
  }

  const PhaseId currentId = currentPlan.phases[currentPhaseIndex].phaseId;

  switch (currentId) {
    case PhaseId::NS_GREEN:
    case PhaseId::EW_GREEN: {
      bool gotPlan = syncChannel_.WaitForPlan(deadline_);

      if (gotPlan) {
        logger.LogInfo() << "New plan signaled during " << PhaseName(currentId)
                         << ", pending until ALL_RED";
        hasNewPlan = true;
        // Emergency chỉ biết được sau khi consume — nhưng đây là lúc nên "peek"
        // sớm để log kịp thời. Nếu cần biết ngay, cân nhắc thêm hàm
        // PeekEmergencyFlags() ở PlanSyncChannel thay vì consume sớm (tránh mất
        // plan nếu đọc 2 lần).
      } else {
        remainingTimeMs -= 1000;
        logger.LogDebug() << PhaseName(currentId)
                          << " tick, remainingTimeMs=" << remainingTimeMs;
      }
      break;
    }

    case PhaseId::YELLOW: {
      clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline_, nullptr);
      remainingTimeMs -= 1000;
      logger.LogDebug() << "YELLOW tick, remainingTimeMs=" << remainingTimeMs;
      break;
    }

    case PhaseId::ALL_RED: {
      clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline_, nullptr);

      if (hasNewPlan) {
        PlanData newPlan;
        if (syncChannel_.ConsumePendingPlan(newPlan)) {
          logger.LogInfo()
              << "Applying new timing plan during ALL_RED, sourcePlanId="
              << newPlan.sourcePlanId;
          if (newPlan.isEmergencyNS || newPlan.isEmergencyEW) {
            logger.LogWarn()
                << "Emergency request applied (NS=" << newPlan.isEmergencyNS
                << ", EW=" << newPlan.isEmergencyEW << ")";
          }
          currentPlan = newPlan;
          currentPhaseIndex = 0;
          isEmergencyNS = newPlan.isEmergencyNS;
          isEmergencyEW = newPlan.isEmergencyEW;
        }
        hasNewPlan = false;
      }
      remainingTimeMs -= 1000;
      logger.LogDebug() << "ALL_RED tick, remainingTimeMs=" << remainingTimeMs;
      break;
    }
  }

  if (remainingTimeMs <= 0 && currentPlan.phaseCount > 0) {
    const uint8_t nextIndex = (currentPhaseIndex + 1) % currentPlan.phaseCount;
    const PhaseId nextId = currentPlan.phases[nextIndex].phaseId;
    logger.LogInfo() << "Transition " << PhaseName(currentId) << " -> "
                     << PhaseName(nextId);
    currentPhaseIndex = nextIndex;
    remainingTimeMs = currentPlan.phases[nextIndex].durationMs;
  }
}