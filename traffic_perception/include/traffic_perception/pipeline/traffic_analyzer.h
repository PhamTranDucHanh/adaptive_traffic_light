#ifndef PIPELINE_TRAFFIC_ANALYZER_H
#define PIPELINE_TRAFFIC_ANALYZER_H

#include <cstdint>

#include "traffic_perception/core/types.h"

// Forward declaration to avoid circular dependency
class MultiLaneViewer;

class TrafficAnalyzer {
 public:
  int32_t ZoneId;

 private:
  MultiLaneViewer *ViewerPtr;

 public:
  void initViewer(MultiLaneViewer *v);
  void trackAndAnalyze(FrameContext &ctx);
};

#endif  // PIPELINE_TRAFFIC_ANALYZER_H
