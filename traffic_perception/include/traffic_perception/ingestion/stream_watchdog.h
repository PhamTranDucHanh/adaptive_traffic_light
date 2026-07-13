#ifndef INGESTION_STREAM_WATCHDOG_H
#define INGESTION_STREAM_WATCHDOG_H

#include "traffic_perception/core/types.h"
#include "traffic_perception/ingestion/stream_worker.h"

class StreamWatchdog {
 public:
  static void monitorHealth(const StreamWorker &worker);
  static void recoverStream(StreamWorker &worker, AppConfig cfg);
};

#endif  // INGESTION_STREAM_WATCHDOG_H
