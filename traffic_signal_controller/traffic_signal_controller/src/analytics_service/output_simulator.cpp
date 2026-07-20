#include "traffic_signal_controller/output_simulator.h"

#include "score/mw/log/logger.h"
#include "traffic_signal_controller/common/logging_contexts.h"

namespace {

score::mw::log::Logger& Logger() {
  static auto& logger =
      score::mw::log::CreateLogger(ctrl::logging::kCtxOut, "Output Simulator");
  return logger;
}

} 

void OutputSimulator::publish(const SignalDisplay& display) const {
  Logger().LogInfo() << "phase=" << phaseName(display.phaseId)
                     << ", remaining=" << display.remainingTimeMs << " ms";
}
const char* OutputSimulator::phaseName(const PhaseId phaseId) noexcept {
  switch (phaseId) {
    case PhaseId::NS_GREEN:
      return "NS_GREEN";

    case PhaseId::EW_GREEN:
      return "EW_GREEN";

    case PhaseId::YELLOW:
      return "YELLOW";

    case PhaseId::ALL_RED:
      return "ALL_RED";
  }

  return "UNKNOWN";
}