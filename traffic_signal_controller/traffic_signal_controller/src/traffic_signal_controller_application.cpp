#include "traffic_signal_controller/traffic_signal_controller_application.h"

#include <chrono>
#include <cstdlib>
#include <iostream>

#include <score/concurrency/interruptible_wait.h>

namespace traffic_signal_controller {

std::int32_t TrafficSignalControllerApplication::Initialize(
    const score::mw::lifecycle::ApplicationContext& context) {
  (void)context;
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  initialized_ = service_.initialize();
  return initialized_ ? EXIT_SUCCESS : EXIT_FAILURE;
}

std::int32_t TrafficSignalControllerApplication::Run(
    const score::cpp::stop_token& stopToken) {
  if (!initialized_) {
    return EXIT_FAILURE;
  }

  const auto period = service_.period();
  auto nextRelease =
      std::chrono::steady_clock::now() + period;
  std::int32_t exitCode{EXIT_SUCCESS};

  while (!stopToken.stop_requested()) {
    if (score::concurrency::wait_until(stopToken, nextRelease)) {
      break;
    }

    if (!service_.runCycle()) {
      std::cerr << "[CONTROLLER][RUN][ERROR] tick failed\n";
      exitCode = EXIT_FAILURE;
      break;
    }

    nextRelease += period;
    const auto now = std::chrono::steady_clock::now();
    while (nextRelease <= now) {
      nextRelease += period;
    }
  }

  const bool safeShutdown = service_.shutdown();
  initialized_ = false;
  if (!safeShutdown) {
    std::cerr
        << "[CONTROLLER][STOP][ERROR] ALL_RED not confirmed\n";
    exitCode = EXIT_FAILURE;
  }
  return exitCode;
}

}  // namespace traffic_signal_controller