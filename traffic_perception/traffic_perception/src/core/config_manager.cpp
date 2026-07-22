#include "traffic_perception/core/config_manager.h"
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>

namespace traffic_perception {

using json = nlohmann::json;

bool ConfigManager::loadConfig() {
  std::cout << "[ConfigManager] loadConfig() called with file: " << ConfigFile << '\n';
  
  std::ifstream f(ConfigFile);
  if (!f.is_open()) {
      std::cerr << "Failed to open config file: " << ConfigFile << '\n';
      return false;
  }
  json data = json::parse(f);

  CachedConfig.CapturePeriod = std::chrono::milliseconds(data.value("capture_period_ms", 200));
  CachedConfig.PipelinePeriod = std::chrono::milliseconds(data.value("pipeline_period_ms", 100));
  CachedConfig.ViewerPeriod = std::chrono::milliseconds(data.value("viewer_period_ms", 100));
  CachedConfig.modelPath = data["model"].get<std::string>();

  auto lanes = data["lanes"];
  for (size_t i = 0; i < NUM_LANES; ++i) {
      auto lane = lanes[i];
      std::string dirStr = lane["direction"];
      Direction dir = Direction::North;
      if (dirStr == "North") dir = Direction::North;
      else if (dirStr == "South") dir = Direction::South;
      else if (dirStr == "East") dir = Direction::East;
      else if (dirStr == "West") dir = Direction::West;

      std::vector<cv::Point> points;
      for (auto& p : lane["roi"]) {
          points.push_back({p[0], p[1]});
      }

      CachedConfig.lanes[i] = LaneConfig{
          dir,
          Roi{points, "Lane " + std::to_string(i)},
          lane["video"]
      };
  }

  return true;
}

AppConfig& ConfigManager::getConfig() {
  return CachedConfig;
}

}  // namespace traffic_perception
