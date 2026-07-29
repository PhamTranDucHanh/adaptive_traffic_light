#include "traffic_perception/io/telemetry_manager.h"
#include "score/mw/log/logging.h"

#include <iostream>

namespace traffic_perception {

void TelemetryManager::showTelemetryMetrics(FrameContext &ctx) const {
  score::mw::log::LogDebug()
      << "[TelemetryManager] showTelemetryMetrics() - Total Cycles: "
      << TotalCycles << ", Count: " << ctx.Detections.size() << '\n';
}

}  // namespace traffic_perception
