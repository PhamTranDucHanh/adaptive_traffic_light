#include <score/mw/lifecycle/runapplication.h>

#include "traffic_signal_controller/traffic_signal_controller_application.h"

int main(int argc, char** argv) {
  return score::mw::lifecycle::run_application<
      traffic_signal_controller::TrafficSignalControllerApplication>(
          argc, argv);
}