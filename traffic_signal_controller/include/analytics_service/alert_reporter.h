#ifndef ALERT_REPORT_H
#define ALERT_REPORT_H

#include <common/config.h>

class AlertReporter {
 public:
  void receiveAlertMetrics(const AlertMetrics& data);

  bool checkThreshold();

  AlertNotification createAlert();

 private:
  int64_t latencyThresholdNs{};
  int64_t emergencyLatencyThresholdNs{};
};

#endif