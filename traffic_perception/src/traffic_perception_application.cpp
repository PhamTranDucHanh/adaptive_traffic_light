#include "traffic_perception/traffic_perception_application.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

#include <score/concurrency/interruptible_wait.h>

namespace traffic_perception {

std::int32_t TrafficPerceptionApplication::Initialize(
    const score::mw::lifecycle::ApplicationContext& context) {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  const std::string configPath = context.get_argument("--config");
  initialized_ = service_.initialize(configPath);
  return initialized_ ? EXIT_SUCCESS : EXIT_FAILURE;
}

std::int32_t TrafficPerceptionApplication::Run(
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
      std::cerr << "[PERCEPTION][RUN][ERROR] cycle failed\n";
      exitCode = EXIT_FAILURE;
      break;
    }

    nextRelease += period;
    const auto now = std::chrono::steady_clock::now();
    while (nextRelease <= now) {
      nextRelease += period;
    }
  }

  service_.shutdown();
  initialized_ = false;
  return exitCode;
}

}  // namespace traffic_perception
