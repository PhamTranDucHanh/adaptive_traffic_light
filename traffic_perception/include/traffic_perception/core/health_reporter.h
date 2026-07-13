#ifndef HEALTH_REPORTER_H
#define HEALTH_REPORTER_H

#include <vector>

class HealthReporter {
 private:
  std::vector<bool> signalBuffer;

 public:
  void sendStartSignal();
  void sendEndSignal();
};

#endif  // HEALTH_REPORTER_H
