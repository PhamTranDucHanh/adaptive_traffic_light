#include "traffic_perception/io/telemetry_manager.h"

#include <iostream>

void TelemetryManager::showTelemetryMetrics(FrameContext &ctx) const {
  std::cout << "[TelemetryManager] showTelemetryMetrics() - Total Cycles: "
               "Vehicle "
            << TotalCycles << ", Count: " << ctx.VehicleCount;
}
