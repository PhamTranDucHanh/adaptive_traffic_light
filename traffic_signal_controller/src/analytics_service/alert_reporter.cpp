#include "analytics_service/alert_reporter.h"

void AlertReporter::receiveAlertMetrics(const AlertMetrics& data) {}

bool AlertReporter::checkThreshold() { return false; }

AlertNotification AlertReporter::createAlert() { return {}; }
