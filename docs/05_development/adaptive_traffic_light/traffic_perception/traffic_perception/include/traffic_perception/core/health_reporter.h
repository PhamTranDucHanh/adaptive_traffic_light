#ifndef HEALTH_REPORTER_H
#define HEALTH_REPORTER_H

#include <vector>

namespace traffic_perception {

class HealthReporter {
 private:
  std::vector<bool> signalBuffer;

 public:
  void sendStartSignal();
  void sendEndSignal();
};

}  // namespace traffic_perception

#endif  // HEALTH_REPORTER_H
