#include "traffic_signal_controller/controller_module.h"

#include <iostream>

namespace traffic_signal_controller {

ControllerModule::~ControllerModule() {
  if (initialized_) {
    (void)shutdown();
  }
}

bool ControllerModule::initialize() {
  if (initialized_) {
    return true;
  }

  // TODO(domain):
  // - open TimingPlan receive-latest transport;
  // - open simulator/hardware output driver;
  // - initialize Timer và FSM;
  // - prepare last-valid/default-safe plan.
  planLoadedIntoFsm_ = false;
  hasConfirmedOutput_ = false;
  appliedPhase_ = PhaseId::ALL_RED;

  // Readiness chỉ được report sau khi safe output đã được xác nhận.
  if (!applySignalOutput(makeAllRedResult())) {
    std::cerr
        << "[CONTROLLER_MODULE][INIT][ERROR] ALL_RED not confirmed\n";
    rollbackDomainResources();
    return false;
  }

  initialized_ = true;
  return true;
}

ControllerCycleResult ControllerModule::runCycle() {
  if (!initialized_) {
    return ControllerCycleResult{false, appliedPhase_};
  }

  PlanData incomingPlan{};
  const PlanReceiveStatus receiveStatus =
      tryReceiveLatestPlan(incomingPlan);
  if (receiveStatus == PlanReceiveStatus::kTransportError) {
    return ControllerCycleResult{false, appliedPhase_};
  }

  if (receiveStatus == PlanReceiveStatus::kReceived) {
    planReceiver_.receivePlan(incomingPlan);

    if (!planReceiver_.validatePlan()) {
      planReceiver_.reject();
    } else {
      const PlanData validatedPlan = planReceiver_.forwardPlan();
      if (applyValidatedPlanToFsm(validatedPlan)) {
        planReceiver_.accept();
        planLoadedIntoFsm_ = true;
      } else {
        // Không acknowledge plan chưa thực sự vào FSM.
        planReceiver_.reject();
      }
    }
  }

  const TimerTick tick = timer_.tick();
  FSMResult requestedResult = fsmEngine_.processTick(tick);

  // FSM stub hiện default thành NS_GREEN. Không được forward giá trị đó.
  if (!planLoadedIntoFsm_) {
    requestedResult = makeAllRedResult();
  }

  if (!applySignalOutput(requestedResult)) {
    return ControllerCycleResult{false, appliedPhase_};
  }

  publisherCollector_.receiveSignalOutput(requestedResult.output);

  // TODO(domain): publish metrics qua bounded/non-blocking transport tới
  // Analytics process riêng; không gọi Analytics implementation trực tiếp.
  const DataCollect data = publisherCollector_.packageData();
  publisherCollector_.publishToAnalytics(data);

  return ControllerCycleResult{true, appliedPhase_};
}

bool ControllerModule::shutdown() {
  if (!initialized_) {
    return safeOutputConfirmed();
  }

  // PoC hiện không rời ALL_RED. Trước khi bật green thật, thay bằng safe
  // shutdown sequence được safety owner duyệt và hardware-confirm.
  const bool safeOutputConfirmed =
      applySignalOutput(makeAllRedResult());
  if (!safeOutputConfirmed) {
    hasConfirmedOutput_ = false;
    std::cerr
        << "[CONTROLLER_MODULE][STOP][ERROR] ALL_RED not confirmed\n";
  }

  // TODO(domain): close plan transport/output driver sau safe-output attempt.
  // Production vẫn cần independent fail-safe output/watchdog.
  rollbackDomainResources();
  planLoadedIntoFsm_ = false;
  initialized_ = false;
  return safeOutputConfirmed;
}

std::chrono::milliseconds ControllerModule::period() const noexcept {
  return std::chrono::milliseconds{TIMER_INTERVAL_MS};
}

PhaseId ControllerModule::appliedPhase() const noexcept {
  return appliedPhase_;
}

bool ControllerModule::safeOutputConfirmed() const noexcept {
  return hasConfirmedOutput_ &&
         appliedPhase_ == PhaseId::ALL_RED;
}

PlanReceiveStatus ControllerModule::tryReceiveLatestPlan(
    PlanData& plan) {
  (void)plan;
  // TODO(domain): bounded, non-blocking receive-latest transport.
  // Phân biệt rõ no-data với transport error.
  return PlanReceiveStatus::kNoData;
}

bool ControllerModule::applyValidatedPlanToFsm(
    const PlanData& plan) {
  (void)plan;
  // TODO(domain): thêm API bool SignalFSMEngine::applyPlan(const PlanData&).
  // Giữ false cho tới khi FSM thật sự sở hữu validated plan.
  return false;
}

bool ControllerModule::applySignalOutput(
    const FSMResult& result) {
  // PoC safe simulator cố ý chặn mọi output khác ALL_RED.
  if (result.output.phaseId != PhaseId::ALL_RED) {
    // Conservative PoC policy: failed/blocked apply invalidates confirmation.
    hasConfirmedOutput_ = false;
    std::cerr
        << "[CONTROLLER_MODULE][OUTPUT][BLOCKED] "
           "real output driver is not implemented\n";
    return false;
  }

  // TODO(domain): thay assignment bằng một output-driver call duy nhất.
  // Chỉ set true sau apply/confirmation thành công; mọi driver error phải
  // set hasConfirmedOutput_ = false trước khi return false.
  appliedPhase_ = PhaseId::ALL_RED;
  hasConfirmedOutput_ = true;
  return true;
}

void ControllerModule::rollbackDomainResources() noexcept {
  // TODO(domain): deterministic close/rollback cho mọi opened resource.
}

FSMResult ControllerModule::makeAllRedResult() const {
  FSMResult result{};
  result.output.phaseId = PhaseId::ALL_RED;
  result.output.remainingMs = 0U;
  result.display.phaseId = PhaseId::ALL_RED;
  result.display.remainingTimeMs = 0U;
  return result;
}

}  // namespace traffic_signal_controller

