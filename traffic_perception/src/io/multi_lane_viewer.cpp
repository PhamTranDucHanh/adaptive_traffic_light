#include "traffic_perception/io/multi_lane_viewer.h"

#include <iostream>

namespace traffic_perception {

void MultiLaneViewer::updateLaneView(FrameContext &ctx) {
  std::cout << "[MultiLaneViewer] updateLaneView() called for Lane: "
            << ctx.LaneId << ", FrameId: " << ctx.FrameId
            << ", Vehicles: " << ctx.VehicleCount << '\n';
}

}  // namespace traffic_perception
