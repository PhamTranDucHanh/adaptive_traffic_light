#include <score/mw/lifecycle/runapplication.h>

#include "signal_control_application.h"

int main(int argc, char** argv) {
  return score::mw::lifecycle::run_application<
      signal_control_demo::SignalControlApplication>(argc, argv);
}
