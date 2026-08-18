#ifndef TRAFFIC_SIGNAL_CONTROLLER_SIGNAL_CONTROL_APP_H_
#define TRAFFIC_SIGNAL_CONTROLLER_SIGNAL_CONTROL_APP_H_

#include <pthread.h>

#include <atomic>
#include <cstdint>

#include <score/mw/lifecycle/application.h>

#include "analytics_service/output_simulator.h"
#include "common/health_reporter.h"
#include "common/plan_sync_channel.h"
#include "traffic_signal_controller/mq_timing_plan_receiver_worker.h"
#include "traffic_signal_controller/plan_receiver.h"
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
  static void* FsmWorkerEntry(void* argument) noexcept;
  static void* PlanReceiverWorkerEntry(void* argument) noexcept;
  static void* OutputWorkerEntry(void* argument) noexcept;

  bool StartFsmWorker() noexcept;
  bool StartPlanReceiverWorker() noexcept;
  bool StartOutputWorker() noexcept;

  void RunFsmWorker() noexcept;
  void RunPlanReceiverWorker() noexcept;
  void RunOutputWorker() noexcept;

  void StopPlanReceiverWorker() noexcept;
  void StopFsmWorker() noexcept;
  void StopOutputWorker() noexcept;
  void WriteAnalyticsReport() const;

  HealthReporter healthReporter_{};

  PlanSyncChannel planSyncChannel_{};
  PlanReceiver planReceiver_{planSyncChannel_};
  MqTimingPlanReceiverWorker mqTimingPlanReceiverWorker_{planReceiver_};
  SignalFSMEngine signalFsmEngine_{planSyncChannel_};
  OutputSimulator outputSimulator_{};

  pthread_t fsmWorker_{};
  pthread_t planReceiverWorker_{};
  pthread_t outputWorker_{};
  bool fsmWorkerCreated_{false};
  bool planReceiverWorkerCreated_{false};
  bool outputWorkerCreated_{false};

  std::atomic_bool fsmRunning_{false};
  std::atomic_bool fsmFailed_{false};
  std::atomic_bool planReceiverRunning_{false};
  std::atomic_bool planReceiverFailed_{false};

  std::uint64_t cycleCount_{0U};
  bool initialized_{false};
};

}  // namespace traffic_signal_controller

#endif  // TRAFFIC_SIGNAL_CONTROLLER_SIGNAL_CONTROL_APP_H_
