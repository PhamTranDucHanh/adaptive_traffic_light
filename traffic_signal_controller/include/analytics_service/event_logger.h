#ifndef EVENT_LOGGER_H
#define EVENT_LOGGER_H

#include <common/config.h>

class EventLogger {
 public:
  HealthStatus sendHeartbeat() const;

  void receiveDataCollect(const DataCollect& data);

  void appendLogEntry();

 private:
  LogEntry formatLogEntry(const DataCollect& data);
};

#endif