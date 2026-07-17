#ifndef ANALYTICS_H
#define ANALYTICS_H

#include <common/config.h>

class Analytics {
 public:
  void dispatchAll();

 private:
  void computeLatency();

  void computeStatistics();

};

#endif