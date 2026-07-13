#include "common/health_reporter.h"

void HealthReporter::receiveHealthMetrics(const HealthStatus& metrics) {}

void HealthReporter::requestHeartbeat() {}

bool HealthReporter::checkHealth() { return true; }

HealthStatus HealthReporter::createHealthStatus() { return {}; }
