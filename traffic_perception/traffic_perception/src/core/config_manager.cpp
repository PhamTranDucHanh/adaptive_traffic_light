#include "traffic_perception/core/config_manager.h"
#include <iostream>

namespace traffic_perception {

bool ConfigManager::loadConfig() {
  std::cout << "[ConfigManager] loadConfig() called with file: " << ConfigFile
            << '\n';
  CachedConfig.trafficVidSources = {"rtsp://stream1", "rtsp://stream2", "rtsp://stream3",
                           "rtsp://stream4"};

  const std::vector<cv::Point> kDefaultRoi = {
      {0, 250},   // Bottom-left
      {1740, 0},    // Top-left
      {630, 1080},   // Top-right
      {1740, 1080}   // Bottom-right
  };

  for (int i = 0; i < 4; ++i) {
      CachedConfig.laneRois[i] = Roi{
          kDefaultRoi,
          "Lane " + std::to_string(i)
      };
  }

  CachedConfig.ModelPath = "yolov8_model.onnx";
  CachedConfig.MaxQueueSize = 1;
  constexpr int32_t targetFpsDefault = 30;
  CachedConfig.TargetFps = targetFpsDefault;
  return true;
}

AppConfig ConfigManager::getConfig() {
  std::cout << "[ConfigManager] getConfig() called" << '\n';
  return CachedConfig;
}

}  // namespace traffic_perception
