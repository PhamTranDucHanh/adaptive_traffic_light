#include "traffic_perception/pipeline/traffic_analyzer.h"

#include <iostream>

namespace traffic_perception {

void TrafficAnalyzer::initViewer() {
  std::cout << "[TrafficAnalyzer] initViewer() called" << '\n';
}

void TrafficAnalyzer::trackAndAnalyze(FrameContext &ctx) {
  std::cout << "[TrafficAnalyzer] trackAndAnalyze() called for Lane "
            << ctx.LaneId << ", FrameId " << (ctx.CapturedFrame ? ctx.CapturedFrame->FrameId : -1) << '\n';
}

}  // namespace traffic_perception
