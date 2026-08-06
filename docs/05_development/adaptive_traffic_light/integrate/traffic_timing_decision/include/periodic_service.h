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
  bool runDecisionCycle();
  std::uint32_t periodMs() const;

 private:
  enum class Configuration : std::uint32_t {
    kPeriodMilliseconds = 250U,
    // 240 cycles x 250 ms = 60 seconds. Preserve the existing wall-clock input
    // timeout while increasing the decision polling frequency.
    kMaximumConsecutiveSnapshotMisses = 240U,
  };

  std::uint32_t periodMs_{
      static_cast<std::uint32_t>(Configuration::kPeriodMilliseconds)};
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
