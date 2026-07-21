#include "traffic_perception/io/telemetry_manager.h"

#include <iostream>

namespace traffic_perception {

void TelemetryManager::showTelemetryMetrics(FrameContext &ctx) const {
  std::cout << "[TelemetryManager] showTelemetryMetrics() - Total Cycles: "
            << TotalCycles << ", Count: " << ctx.Detections.size() << '\n';
}

}  // namespace traffic_perception
