#include "traffic_signal_controller/controller_periodic_service.h"

namespace traffic_signal_controller {

bool ControllerPeriodicService::initialize() {
  if (running_) {
    return true;
  }

  // Controller xác nhận ALL_RED trước khi Lifecycle report Running.
  if (!module_.initialize()) {
    return false;
  }

  if (!health_.initialize()) {
    (void)module_.shutdown();
    return false;
  }

  running_ = true;
  return true;
}

bool ControllerPeriodicService::runCycle() {
  if (!running_ || !health_.beginTick()) {
    return false;
  }

  const ControllerCycleResult result = module_.runCycle();
  return health_.finishTick(result.appliedPhase, result.success);
}

bool ControllerPeriodicService::shutdown() {
  if (!running_) {
    return module_.safeOutputConfirmed();
  }

  const bool safeOutputConfirmed = module_.shutdown();
  bool healthStateConsistent{false};
  if (safeOutputConfirmed) {
    healthStateConsistent =
        health_.reportAppliedPhase(module_.appliedPhase());
  }

  health_.shutdown();
  running_ = false;
  return safeOutputConfirmed && healthStateConsistent;
}

std::chrono::milliseconds
ControllerPeriodicService::period() const noexcept {
  return module_.period();
}

}  // namespace traffic_signal_controller