#include "traffic_perception/ingestion/stream_watchdog.h"

#include <cstdio>

void StreamWatchdog::monitorHealth(const StreamWorker &worker) {
  (void)worker;
  std::cout << "[StreamWatchdog] monitorHealth() called\n";
}

void StreamWatchdog::recoverStream(StreamWorker &worker, AppConfig cfg) {
  std::cout << "[StreamWatchdog] recoverStream() called\n";
  worker.initStream(std::move(cfg), 0);  // Temporary stub call
}
