#include "traffic_perception/ingestion/stream_watchdog.h"

#include <cstdio>
#include <iostream>

#include "score/mw/log/logging.h"

namespace traffic_perception {

void StreamWatchdog::monitorHealth(const StreamWorker &worker) {
  (void)worker;
  score::mw::log::LogDebug() << "[StreamWatchdog] monitorHealth() called\n";
}

void StreamWatchdog::recoverStream(StreamWorker &worker) {
  score::mw::log::LogDebug() << "[StreamWatchdog] recoverStream() called\n";
  worker.restart();
}

}  // namespace traffic_perception
