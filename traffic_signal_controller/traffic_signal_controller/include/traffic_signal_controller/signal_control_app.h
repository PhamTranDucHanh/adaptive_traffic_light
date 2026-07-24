#ifndef TRAFFIC_SIGNAL_CONTROLLER_SIGNAL_CONTROL_APP_H
#define TRAFFIC_SIGNAL_CONTROLLER_SIGNAL_CONTROL_APP_H

#include <atomic>
#include <cstdint>
#include <thread>

#include <score/mw/lifecycle/application.h>

#include "common/health_reporter.h"
#include "analytics_service/output_simulator.h"
#include "traffic_signal_controller/plan_receiver.h"
#include "common/plan_sync_channel.h"
#include "traffic_signal_controller/signal_fsm_engine.h"


namespace traffic_signal_controller {

class SignalControlApplication final
    : public score::mw::lifecycle::Application {
 public:
  SignalControlApplication() = default;

  ~SignalControlApplication() override;

  std::int32_t Initialize(
      const score::mw::lifecycle::ApplicationContext& context) override;

  std::int32_t Run(
      const score::cpp::stop_token& stopToken) override;

 private:
  enum class DemoPlanReceiverState {
    kWaitingForCongestionPlan,
    kWaitingForEmergencyPlan,
    kCompleted,
  };

  void RunFsmWorker() noexcept;
  void StopFsmWorker() noexcept;
  void RunPlanReceiverSimulation();
  void WriteAnalyticsReport() const;

  TimingPlan CreateCongestionPlan() const;
  TimingPlan CreateEmergencyPlan() const;

  HealthReporter healthReporter_{};

  PlanSyncChannel planSyncChannel_{};
  PlanReceiver planReceiver_{planSyncChannel_};
  SignalFSMEngine signalFsmEngine_{planSyncChannel_};
  OutputSimulator outputSimulator_{};

  std::thread fsmWorker_{};

  std::atomic_bool fsmRunning_{false};
  std::atomic_bool fsmFailed_{false};

  std::uint64_t cycleCount_{0U};

  DemoPlanReceiverState demoPlanReceiverState_{
      DemoPlanReceiverState::kWaitingForCongestionPlan};
  bool initialized_{false};
};

}  // namespace traffic_signal_controller

#endif  // TRAFFIC_SIGNAL_CONTROLLER_SIGNAL_CONTROL_APP_H_
