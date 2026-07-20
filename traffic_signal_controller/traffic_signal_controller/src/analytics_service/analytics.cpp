#include "analytics_service/analytics.h"

void Analytics::receiveLogEntry(const LogEntry& entry) {}

void Analytics::computeLatency(const LogEntry& entry) {}

void Analytics::computeStatistics() {}

DashboardData Analytics::formatForDashboard() { return {}; }

AlertMetrics Analytics::formatForAlert() { return {}; }

ReportMetrics Analytics::formatForReport() { return {}; }

void Analytics::dispatchAll() {}

HealthStatus Analytics::sendHeartbeat() const { return {}; }