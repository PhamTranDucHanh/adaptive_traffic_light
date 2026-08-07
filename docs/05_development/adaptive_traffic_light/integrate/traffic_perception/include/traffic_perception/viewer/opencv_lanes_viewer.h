#ifndef TRAFFIC_PERCEPTION_VIEWER_OPENCV_LANES_VIEWER_H_
#define TRAFFIC_PERCEPTION_VIEWER_OPENCV_LANES_VIEWER_H_

#include <array>
#include <cstdint>
#include <opencv2/opencv.hpp>

#include "traffic_perception/core/types.h"
#include "traffic_perception/inference/imodel_backend.h"
#include "traffic_perception/pipeline/analyzer.h"
#include "traffic_ipc/latest_value_queue.h"
#include "traffic_ipc/signal_state_message_v1.h"

namespace traffic_perception {

class OpenCVLanesViewer {
 public:
  bool init(const AppConfig& config, IModelBackend* backend);

  void render(Analyzer& analyzer, int64_t expectedWakeupNs,
              int64_t renderBeginNs);

  void render(Analyzer& analyzer,
              const std::array<cv::Mat, NUM_LANES>& previewFrames,
              int64_t expectedWakeupNs, int64_t renderBeginNs);

  void refreshSignalOverlay(std::int64_t nowNs);

  void shutdown();

 private:
  void pollSignalState(std::int64_t nowNs) noexcept;

  AppConfig config_;
  IModelBackend* backend_{nullptr};
  std::string windowName_;
  std::array<std::string, NUM_LANES> laneLabels_{};
  std::array<cv::Mat, NUM_LANES> lastRenderedFrames_;
  cv::Mat baseCanvas_{};
  traffic_ipc::LatestValueConsumer<traffic_ipc::SignalStateMessageV1>
      signalStateConsumer_{traffic_ipc::kSignalStateQueueName,
                           traffic_ipc::kSignalStateLockName};
  traffic_ipc::SignalStateMessageV1 signalState_{};
  std::int64_t nextSignalStateOpenAttemptNs_{0};
  bool hasSignalState_{false};
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_VIEWER_OPENCV_LANES_VIEWER_H_
