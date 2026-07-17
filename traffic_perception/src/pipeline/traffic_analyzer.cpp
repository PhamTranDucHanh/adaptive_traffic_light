#include "traffic_perception/pipeline/traffic_analyzer.h"

#include <iostream>

#include "traffic_perception/io/multi_lane_viewer.h"

namespace traffic_perception {

void TrafficAnalyzer::initViewer(MultiLaneViewer *viewerPtr) {
  std::cout << "[TrafficAnalyzer] initViewer() called" << '\n';
  ViewerPtr = viewerPtr;
}

void TrafficAnalyzer::trackAndAnalyze(FrameContext &ctx) {
  std::cout << "[TrafficAnalyzer] trackAndAnalyze() called for Lane "
            << ctx.LaneId << ", FrameId " << (ctx.CapturedFrame ? ctx.CapturedFrame->FrameId : -1) << '\n';
  if (ViewerPtr != nullptr) {
    MultiLaneViewer::updateLaneView(ctx);
  }
}

}  // namespace traffic_perception
