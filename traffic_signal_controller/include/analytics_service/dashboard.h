#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <common/config.h>

class Dashboard {
 public:
  void receiveDashboardData(const DashboardData& data);

  DashboardView createDashboardView();

  void sendDashboardView();
};

#endif