#ifndef OUTPUT_SIMULATOR_H_
#define OUTPUT_SIMULATOR_H_

#include "common/config.h"

class OutputSimulator final {
 public:
  void publish(const SignalDisplay& display) const;

 private:
  static const char* phaseName(PhaseId phaseId) noexcept;
};

#endif  // TRAFFIC_SIGNAL_CONTROLLER_OUTPUT_SIMULATOR_H_