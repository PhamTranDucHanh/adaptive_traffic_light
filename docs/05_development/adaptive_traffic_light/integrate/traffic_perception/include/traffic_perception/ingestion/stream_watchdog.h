#ifndef INGESTION_STREAM_WATCHDOG_H
#define INGESTION_STREAM_WATCHDOG_H

#include <iostream>
#include <utility>

#include "traffic_perception/core/types.h"
#include "traffic_perception/ingestion/stream_worker.h"

namespace traffic_perception {

class StreamWatchdog {
 public:
  static void monitorHealth(const StreamWorker &worker);
  static void recoverStream(StreamWorker &worker);
};

}  // namespace traffic_perception

#endif  // INGESTION_STREAM_WATCHDOG_H
