#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <string>

#include "traffic_perception/core/types.h"

class ConfigManager {
 private:
  std::string ConfigFile;
  AppConfig CachedConfig;

 public:
  bool loadConfig();
  AppConfig getConfig();
};

#endif  // CONFIG_MANAGER_H
