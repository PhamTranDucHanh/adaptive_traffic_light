#include "traffic_perception/core/health_reporter.h"
#include "score/mw/log/logging.h"

#include <iostream>

namespace traffic_perception {

void HealthReporter::sendStartSignal() {
  score::mw::log::LogDebug() << "[HealthReporter] sendStartSignal() called" << '\n';
  signalBuffer.push_back(true);
}

void HealthReporter::sendEndSignal() {
  score::mw::log::LogDebug() << "[HealthReporter] sendEndSignal() called" << '\n';
  signalBuffer.push_back(false);
}

}  // namespace traffic_perception
