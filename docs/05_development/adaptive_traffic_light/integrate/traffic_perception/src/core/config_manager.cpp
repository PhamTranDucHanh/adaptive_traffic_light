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

  if (data.contains("video_resolution")) {
    CachedConfig.videoResolution.width =
        data["video_resolution"].value("width", 1920);
    CachedConfig.videoResolution.height =
        data["video_resolution"].value("height", 1080);
  }

  if (data["threading"].contains("stream_workers") &&
      data["threading"]["stream_workers"].is_array()) {
    auto workersJson = data["threading"]["stream_workers"];
    for (size_t i = 0; i < NUM_LANES && i < workersJson.size(); ++i) {
      CachedConfig.Threading.StreamWorkers[i].Core =
          workersJson[i].value("core", -1);
      CachedConfig.Threading.StreamWorkers[i].Policy =
          workersJson[i].value("policy", "SCHED_RR");
      CachedConfig.Threading.StreamWorkers[i].Priority =
          workersJson[i].value("priority", 70);
    }
  } else if (data["threading"].contains("stream")) {
    // Fallback for legacy single "stream" config
    for (size_t i = 0; i < NUM_LANES; ++i) {
      CachedConfig.Threading.StreamWorkers[i].Core =
          data["threading"]["stream"].value("core", -1);
      CachedConfig.Threading.StreamWorkers[i].Policy =
          data["threading"]["stream"].value("policy", "SCHED_RR");
      CachedConfig.Threading.StreamWorkers[i].Priority =
          data["threading"]["stream"].value("priority", 70);
    }
  }

  if (data["threading"].contains("pipeline")) {
    CachedConfig.Threading.Pipeline.Core =
        data["threading"]["pipeline"].value("core", -1);
    CachedConfig.Threading.Pipeline.Policy =
        data["threading"]["pipeline"].value("policy", "SCHED_RR");
    CachedConfig.Threading.Pipeline.Priority =
        data["threading"]["pipeline"].value("priority", 70);
  }

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
