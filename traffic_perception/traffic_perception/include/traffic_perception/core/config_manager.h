#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>

#include "traffic_perception/core/types.h"

namespace traffic_perception {

class ConfigManager {
 private:
  std::string ConfigFile;
  AppConfig CachedConfig;

 public:
  explicit ConfigManager(std::string configFile) : ConfigFile(std::move(configFile)) {}
  bool loadConfig();
  AppConfig& getConfig();
};

}  // namespace traffic_perception

#endif  // CONFIG_MANAGER_H
