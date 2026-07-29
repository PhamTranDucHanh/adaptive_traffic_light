#include "traffic_perception/core/config_manager.h"

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#include "score/mw/log/logging.h"

namespace traffic_perception {

using json = nlohmann::json;

bool ConfigManager::loadConfig() {
  score::mw::log::LogDebug()
      << "[ConfigManager] loadConfig() called with file: " << ConfigFile
      << '\n';

  std::ifstream f(ConfigFile);
  if (!f.is_open()) {
    std::cerr << "Failed to open config file: " << ConfigFile << '\n';
    return false;
  }
  json data = json::parse(f);

  CachedConfig.Threading.Stream.Policy = data["threading"]["stream"]["policy"];
  CachedConfig.Threading.Stream.Priority =
      data["threading"]["stream"]["priority"];

  CachedConfig.Threading.Pipeline.Policy =
      data["threading"]["pipeline"]["policy"];
  CachedConfig.Threading.Pipeline.Priority =
      data["threading"]["pipeline"]["priority"];

  CachedConfig.CapturePeriod =
      std::chrono::milliseconds(data.value("capture_period_ms", 200));
  CachedConfig.PipelinePeriod =
      std::chrono::milliseconds(data.value("pipeline_period_ms", 3000));
  CachedConfig.ViewerPeriod =
      std::chrono::milliseconds(data.value("viewer_period_ms", 3000));
  CachedConfig.CapturePhase =
      std::chrono::milliseconds(data.value("capture_phase_ms", 0));
  CachedConfig.PipelinePhase =
      std::chrono::milliseconds(data.value("pipeline_phase_ms", 50));
  CachedConfig.ViewerPhase =
      std::chrono::milliseconds(data.value("viewer_phase_ms", 260));
  CachedConfig.modelPath = data["model"].get<std::string>();
  

  auto lanes = data["lanes"];
  for (size_t i = 0; i < NUM_LANES; ++i) {
    auto lane = lanes[i];
    std::string dirStr = lane["direction"];
    Direction dir = Direction::North;
    if (dirStr == "North")
      dir = Direction::North;
    else if (dirStr == "South")
      dir = Direction::South;
    else if (dirStr == "East")
      dir = Direction::East;
    else if (dirStr == "West")
      dir = Direction::West;

    std::vector<cv::Point> points;
    for (auto& p : lane["roi"]) {
      points.push_back({p[0], p[1]});
    }

    CachedConfig.lanes[i] = LaneConfig{
        dir, Roi{points, "Lane " + std::to_string(i)}, lane["video"]};
  }

  return true;
}

AppConfig& ConfigManager::getConfig() { return CachedConfig; }

}  // namespace traffic_perception
