#include "traffic_perception/ingestion/stream_watchdog.h"

#include <cstdio>
#include <iostream>

namespace traffic_perception {

void StreamWatchdog::monitorHealth(const StreamWorker &worker) {
  (void)worker;
  std::cout << "[StreamWatchdog] monitorHealth() called\n";
}

void StreamWatchdog::recoverStream(StreamWorker &worker, AppConfig cfg) {
  std::cout << "[StreamWatchdog] recoverStream() called\n";
  worker.initStream(cfg.trafficVidSources.at(0), 0, nullptr);  // Temporary fix
}

}  // namespace traffic_perception
