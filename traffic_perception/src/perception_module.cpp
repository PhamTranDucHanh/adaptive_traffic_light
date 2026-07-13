#include "traffic_perception/perception_module.h"

#include <iostream>

bool PerceptionModule::initModule(const std::string &configPath) {
  std::cout << "[PerceptionModule] initModule() called with config path: "
            << configPath << "\n";
  if (!configManager.loadConfig()) {
    return false;
  }
  AppConfig config = configManager.getConfig();
  
  int32_t streamId = 0;
  for (auto &worker : workers) {
    worker.initStream(config, streamId++);
  }
  
  // Initialize analyzer & publisher
  analyzer.initViewer(&viewer);
  publisher.initTelemetry(&telemetry);
  publisher.initSender(&snapshotSender);
  return true;
}

void PerceptionModule::startThreads() {
  (void)workers;
  std::cout << "[PerceptionModule] startThreads() called" << '\n';
}

void PerceptionModule::stopThreads() {
  (void)workers;
  std::cout << "[PerceptionModule] stopThreads() called" << '\n';
}
