#ifndef TRAFFIC_TIMING_DECISION_TIMING_DECISION_APPLICATION_H
#define TRAFFIC_TIMING_DECISION_TIMING_DECISION_APPLICATION_H

// #define LOG_NUMBER_CYCLES
// #define RT_THREAD_CHECKING

#ifdef RT_THREAD_CHECKING
#include <pthread.h>

#include <atomic>
#endif

#include <score/mw/lifecycle/application.h>

#include <cstdint>

#include "common.h"
#include "periodic_service.h"

namespace traffic_timing_decision {

class TimingDecisionApplication final
    : public score::mw::lifecycle::Application {
 public:
#ifdef RT_THREAD_CHECKING
  ~TimingDecisionApplication() override;
#endif

  std::int32_t Initialize(
      const score::mw::lifecycle::ApplicationContext& context) override;
  std::int32_t Run(const score::cpp::stop_token& stopToken) override;

 private:
  PeriodicService service_;
  common::PeriodicWait periodicWait_;
#ifdef RT_THREAD_CHECKING
  static void* childThreadEntry(void* application);
  bool startRtChildThread() noexcept;
  void stopRtChildThread() noexcept;
  void runRtChildThread() noexcept;

  pthread_t childThread_{};
  std::atomic_bool childThreadRunning_{false};
  bool childThreadCreated_{false};
#endif
  std::uint64_t cycleCount_{};
  bool initialized_{false};
};

}  // namespace traffic_timing_decision

#endif  // TRAFFIC_TIMING_DECISION_TIMING_DECISION_APPLICATION_H
