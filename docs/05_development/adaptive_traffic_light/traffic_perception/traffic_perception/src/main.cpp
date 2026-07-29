#include <score/mw/lifecycle/runapplication.h>

#include "traffic_perception/traffic_perception_application.h"

int main(int argc, char** argv) {
  return score::mw::lifecycle::run_application<
      traffic_perception::TrafficPerceptionApplication>(argc, argv);
}
