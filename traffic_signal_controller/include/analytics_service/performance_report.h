#ifndef PERFORMANCE_REPORT_H
#define PERFORMANCE_REPORT_H

#include <common/config.h>

class PerformanceReport {
 public:
  void receiveReportMetrics(const ReportMetrics& data);

  ReportData createReport();

  void sendReport();
};

#endif