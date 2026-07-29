#include <score/mw/lifecycle/runapplication.h>

#include "timing_decision_application.h"

int32_t main(int argc, char** argv) {
  return score::mw::lifecycle::run_application<
      traffic_timing_decision::TimingDecisionApplication>(argc, argv);
}
