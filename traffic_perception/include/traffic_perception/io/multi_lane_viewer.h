#ifndef IO_MULTI_LANE_VIEWER_H
#define IO_MULTI_LANE_VIEWER_H

#include <string>

#include "traffic_perception/core/types.h"

namespace traffic_perception {

class MultiLaneViewer {
 public:
  std::string WindowName;

  static void updateLaneView(FrameContext &ctx);
};

}  // namespace traffic_perception

#endif  // IO_MULTI_LANE_VIEWER_H
