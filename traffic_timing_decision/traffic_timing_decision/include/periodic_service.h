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
  uint32_t periodMs() const;

 private:
  uint32_t periodMs_{2500};
  bool running_{false};
  TrafficDataReceiver trafficReceiver;
  DecisionEngine decisionEngine;
  TimingPlanPublisher timingPublisher;
  HealthReporter healthReporter;
};

#endif  // !PERIODIC_SERVICE_H
