#include "periodic_service.h"

//
// Constructor
//
PeriodicService::PeriodicService() = default;

//
// Realtime
//
bool PeriodicService::createPeriodicTimer() { return true; }
void PeriodicService::destroyPeriodicTimer() {}
bool PeriodicService::waitNextPeriod() { return true; }

//
// Decision Pipeline
//
void PeriodicService::runDecisionCycle() {}