#include <score/mw/lifecycle/runapplication.h>

#include "traffic_signal_controller/signal_control_app.h"

int main(int argc, char** argv) {
  return score::mw::lifecycle::run_application<
      traffic_signal_controller::SignalControlApplication>(argc, argv);
}
