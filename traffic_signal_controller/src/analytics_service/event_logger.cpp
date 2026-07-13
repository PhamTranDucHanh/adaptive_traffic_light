#include "analytics_service/event_logger.h"

void EventLogger::receiveDataCollect(const DataCollect& data) {}

void EventLogger::appendLogEntry() {}

LogEntry EventLogger::formatLogEntry(const DataCollect& data) { return {}; }

HealthStatus EventLogger::sendHeartbeat() const { return {}; }