#include "health_reporter.h"

//
// Constructor
//
HealthReporter::HealthReporter() = default;

//
// Decision Cycle
//
void HealthReporter::startDecisionCycle() {}
void HealthReporter::finishDecisionCycle() {}

//
// Report
//
HealthReport HealthReporter::generateHealthReport() const {
  return HealthReport{};
}
bool HealthReporter::publishHealthReport() { return true; }

//
// Internal Helpers
//
void HealthReporter::recordStartTimestamp() {}
void HealthReporter::recordFinishTimestamp() {}
void HealthReporter::calculateExecutionTime() {}
void HealthReporter::evaluateDeadline() {}