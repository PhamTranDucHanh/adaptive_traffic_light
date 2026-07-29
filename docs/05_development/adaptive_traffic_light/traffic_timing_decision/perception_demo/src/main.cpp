#include <score/mw/lifecycle/runapplication.h>

#include "perception_application.h"

int main(int argc, char** argv) {
  return score::mw::lifecycle::run_application<
      perception_demo::PerceptionApplication>(argc, argv);
}
