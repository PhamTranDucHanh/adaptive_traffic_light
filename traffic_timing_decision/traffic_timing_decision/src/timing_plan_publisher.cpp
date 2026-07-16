#include "timing_plan_publisher.h"

//
// Constructor
//
TimingPlanPublisher::TimingPlanPublisher() = default;

//
// Publish
//
bool TimingPlanPublisher::publishTimingPlan(const TimingPlan& plan) {
  (void)plan;
  return true;
}
bool TimingPlanPublisher::publishPreviousTimingPlan() { return true; }

//
// Internal Helpers
//
bool TimingPlanPublisher::waitAck() { return true; }