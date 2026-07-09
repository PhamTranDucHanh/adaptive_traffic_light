#ifndef IO_MULTI_LANE_VIEWER_H
#define IO_MULTI_LANE_VIEWER_H

#include <string>

#include "traffic_perception/core/types.h"

class MultiLaneViewer {
 public:
  std::string WindowName;

  static void updateLaneView(FrameContext &ctx);
};

#endif  // IO_MULTI_LANE_VIEWER_H
