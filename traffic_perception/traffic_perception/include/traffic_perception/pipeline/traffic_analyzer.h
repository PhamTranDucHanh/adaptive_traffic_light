#ifndef PIPELINE_TRAFFIC_ANALYZER_H
#define PIPELINE_TRAFFIC_ANALYZER_H

#include <cstdint>

#include "traffic_perception/core/types.h"

namespace traffic_perception {

// Forward declaration to avoid circular dependency
class OpenCVLanesViewer;

class TrafficAnalyzer {
 public:
  int32_t ZoneId;

 private:

 public:
  void initViewer();
  void trackAndAnalyze(FrameContext &ctx);
};

}  // namespace traffic_perception

#endif  // PIPELINE_TRAFFIC_ANALYZER_H
