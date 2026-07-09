#ifndef HEALTH_REPORTER_H
#define HEALTH_REPORTER_H

#include <common/config.h>

class HealthReporter {
 public:
  void requestHeartbeat();

  void receiveHealthMetrics(const HealthStatus& metrics);

  bool checkHealth();

  HealthStatus createHealthStatus();

 private:
  HealthStatus currentStatus;
};

#endif