#include "traffic_perception/core/health_reporter.h"

#include <iostream>

namespace traffic_perception {

void HealthReporter::sendStartSignal() {
  std::cout << "[HealthReporter] sendStartSignal() called" << '\n';
  signalBuffer.push_back(true);
}

void HealthReporter::sendEndSignal() {
  std::cout << "[HealthReporter] sendEndSignal() called" << '\n';
  signalBuffer.push_back(false);
}

}  // namespace traffic_perception
