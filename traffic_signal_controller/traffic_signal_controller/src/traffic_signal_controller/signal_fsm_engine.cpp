#include "traffic_signal_controller/signal_fsm_engine.h"
#include "common/logging_contexts.h"
#include "score/mw/log/logger.h"


namespace {

score::mw::log::Logger& Logger() {
  static score::mw::log::Logger& logger =
      score::mw::log::CreateLogger(
          ctrl::logging::kCtxFsm,
          "Signal FSM engine");
  return logger;
}

}


FSMResult SignalFSMEngine::processTick(const TimerTick& tick) { return {}; }

void SignalFSMEngine::fsm() {}

HealthStatus SignalFSMEngine::sendHeartbeat() const { return {}; }