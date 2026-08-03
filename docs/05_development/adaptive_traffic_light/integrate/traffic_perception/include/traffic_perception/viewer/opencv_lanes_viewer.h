#ifndef TRAFFIC_PERCEPTION_VIEWER_OPENCV_LANES_VIEWER_H_
#define TRAFFIC_PERCEPTION_VIEWER_OPENCV_LANES_VIEWER_H_

#include <array>
#include <opencv2/opencv.hpp>

#include "traffic_perception/core/types.h"
#include "traffic_perception/inference/imodel_backend.h"
#include "traffic_perception/pipeline/analyzer.h"

namespace traffic_perception {

class OpenCVLanesViewer {
 public:
  bool init(const AppConfig& config, IModelBackend* backend);

  void render(Analyzer& analyzer, int64_t expectedWakeupNs,
              int64_t renderBeginNs);

  void render(Analyzer& analyzer,
              const std::array<cv::Mat, NUM_LANES>& previewFrames,
              int64_t expectedWakeupNs, int64_t renderBeginNs);

  void shutdown();

 private:
  AppConfig config_;
  IModelBackend* backend_{nullptr};
  std::string windowName_;
  std::array<std::string, NUM_LANES> laneLabels_{};
  std::array<cv::Mat, NUM_LANES> lastRenderedFrames_;
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_VIEWER_OPENCV_LANES_VIEWER_H_
