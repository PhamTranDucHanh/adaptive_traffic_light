#ifndef PERIODIC_SERVICE_H
#define PERIODIC_SERVICE_H

#include <cstdint>

#include "decision_engine.h"
#include "health_reporter.h"
#include "timing_plan_publisher.h"
#include "traffic_data_receiver.h"

class PeriodicService {
 public:
  PeriodicService();
  ~PeriodicService() = default;

  bool initialize();
  void shutdown();
  SnapshotWaitStatus waitForSnapshot(TrafficSnapshot& snapshot) noexcept;
  bool runDecisionCycle(SnapshotWaitStatus waitStatus,
                        const TrafficSnapshot& snapshot);
  void requestStop() noexcept;
  std::uint32_t eventWaitTimeoutMs() const noexcept;

 private:
  enum class Configuration : std::uint32_t {
    // The MQ readiness event normally wakes the worker immediately. This
    // bounded timeout only services health/lifecycle and input-loss detection.
    kEventWaitTimeoutMilliseconds = 250U,
    // 240 timeout/retry wake-ups x 250 ms = 60 seconds. Preserve the existing
    // wall-clock input timeout without polling the queue for normal input.
    kMaximumConsecutiveSnapshotMisses = 240U,
  };

  std::uint32_t eventWaitTimeoutMs_{
      static_cast<std::uint32_t>(Configuration::kEventWaitTimeoutMilliseconds)};
  std::uint32_t consecutiveSnapshotMisses_{};
  std::uint32_t maximumConsecutiveSnapshotMisses_{static_cast<std::uint32_t>(
      Configuration::kMaximumConsecutiveSnapshotMisses)};
  bool running_{false};
  TrafficDataReceiver trafficReceiver;
  DecisionEngine decisionEngine;
  TimingPlanPublisher timingPublisher;
  HealthReporter healthReporter;
};

#endif  // !PERIODIC_SERVICE_H
