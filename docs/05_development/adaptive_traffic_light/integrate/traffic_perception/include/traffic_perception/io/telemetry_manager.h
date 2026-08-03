#ifndef IO_TELEMETRY_MANAGER_H
#define IO_TELEMETRY_MANAGER_H

#include <cstdint>

#include "traffic_perception/core/types.h"

namespace traffic_perception {

class TelemetryManager {
 public:
  int32_t TotalCycles;

  void showTelemetryMetrics(FrameContext &ctx) const;
};

}  // namespace traffic_perception

#endif  // IO_TELEMETRY_MANAGER_H
