#ifndef ANALYTICS_H
#define ANALYTICS_H

#include <common/config.h>

class Analytics {
 public:
  void receiveLogEntry(const LogEntry& entry);

  void dispatchAll();

  HealthStatus sendHeartbeat() const;

 private:
  void computeLatency(const LogEntry& entry);

  void computeStatistics();

  DashboardData formatForDashboard();

  AlertMetrics formatForAlert();

  ReportMetrics formatForReport();
};

#endif