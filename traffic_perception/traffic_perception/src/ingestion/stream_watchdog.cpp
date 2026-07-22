#include "traffic_perception/ingestion/stream_watchdog.h"

#include <cstdio>
#include <iostream>

namespace traffic_perception {

void StreamWatchdog::monitorHealth(const StreamWorker &worker) {
  (void)worker;
  std::cout << "[StreamWatchdog] monitorHealth() called\n";
}

void StreamWatchdog::recoverStream(StreamWorker &worker) {
  std::cout << "[StreamWatchdog] recoverStream() called\n";
  worker.restart();
}

}  // namespace traffic_perception
