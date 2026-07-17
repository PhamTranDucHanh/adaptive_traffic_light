#include "traffic_perception/core/config_manager.h"
#include <iostream>

namespace traffic_perception {

bool ConfigManager::loadConfig() {
  std::cout << "[ConfigManager] loadConfig() called with file: " << ConfigFile
            << '\n';
  CachedConfig.RtspUrls = {"rtsp://stream1", "rtsp://stream2", "rtsp://stream3",
                           "rtsp://stream4"};
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
