#ifndef TRAFFIC_SIGNAL_CONTROLLER_CONTROLLER_MODULE_H_
#define TRAFFIC_SIGNAL_CONTROLLER_CONTROLLER_MODULE_H_

#include <chrono>

#include "traffic_signal_controller/plan_receiver.h"
#include "traffic_signal_controller/signal_fsm_engine.h"

namespace traffic_signal_controller {

struct ControllerCycleResult {
  bool success{false};
  PhaseId appliedPhase{PhaseId::ALL_RED};
};

enum class PlanReceiveStatus {
  kNoData,
  kReceived,
  kTransportError,
};

class ControllerModule final {
 public:
  ControllerModule() = default;
  ~ControllerModule();

  ControllerModule(const ControllerModule&) = delete;
  ControllerModule& operator=(const ControllerModule&) = delete;

  bool initialize();
  ControllerCycleResult runCycle();
  bool shutdown();

  std::chrono::milliseconds period() const noexcept;
  PhaseId appliedPhase() const noexcept;
  bool safeOutputConfirmed() const noexcept;

 private:
  PlanReceiveStatus tryReceiveLatestPlan(PlanData& plan);
  bool applyValidatedPlanToFsm(const PlanData& plan);
  bool applySignalOutput(const FSMResult& result);
  void rollbackDomainResources() noexcept;
  FSMResult makeAllRedResult() const;

  ::PlanReceiver planReceiver_{};
  ::Timer timer_{};
  ::SignalFSMEngine fsmEngine_{};
  ::PublisherCollector publisherCollector_{};

  PhaseId appliedPhase_{PhaseId::ALL_RED};
  bool hasConfirmedOutput_{false};
  bool planLoadedIntoFsm_{false};
  bool initialized_{false};
};

}  // namespace traffic_signal_controller

#endif  // TRAFFIC_SIGNAL_CONTROLLER_CONTROLLER_MODULE_H_