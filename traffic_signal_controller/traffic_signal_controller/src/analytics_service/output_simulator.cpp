#include "analytics_service/output_simulator.h"


#include "common/logging_contexts.h"
#include "score/mw/log/logger.h"

namespace {

score::mw::log::Logger& Logger() {
  static score::mw::log::Logger& logger =
      score::mw::log::CreateLogger(
          ctrl::logging::kCtxOut,
          "Output simulator");
  return logger;
}

}

void OutputSimulator::receiveSignalDisplay(const SignalDisplay& display) {}

void OutputSimulator::formatConsole() {}

void OutputSimulator::sendToParticipants() {}