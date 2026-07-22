#include "traffic_perception/core/config_manager.h"
#include <iostream>

namespace traffic_perception {
bool ConfigManager::loadConfig(const std::string& modelPath, const std::array<std::string, NUM_LANES>& videoPaths) {
  std::cout << "[ConfigManager] loadConfig() called with file: " << ConfigFile
            << '\n';

  const std::vector<cv::Point> kDefaultRoi = {
    {0, 561},    
    {0, 1075},     
    {1920, 40},    
    {1700, 0}      
  };

  const std::array<Direction, NUM_LANES> directions = {Direction::North, Direction::South, Direction::East, Direction::West};

  for (size_t i = 0; i < NUM_LANES; ++i) {
      CachedConfig.lanes[i] = LaneConfig{
          directions[i],
          Roi{kDefaultRoi, "Lane " + std::to_string(i)},
          videoPaths[i]
      };
  }

  CachedConfig.modelPath = modelPath;
  return true;
}

AppConfig ConfigManager::getConfig() {
  std::cout << "[ConfigManager] getConfig() called" << '\n';
  return CachedConfig;
}

}  // namespace traffic_perception
