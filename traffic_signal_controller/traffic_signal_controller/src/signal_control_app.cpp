#include "traffic_signal_controller/signal_control_app.h"

#include <chrono>
#include <cstdlib>
#include <iostream>

#include "score/concurrency/interruptible_wait.h"
#include "score/mw/log/rust/stdout_logger_init.h"

namespace traffic_signal_controller {

std::int32_t SignalControlApplication::Initialize(
    const score::mw::lifecycle::ApplicationContext& context) {
  (void)context;
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  // The Lifecycle HealthMonitor implementation logs through the Rust logger.
  // Initialize it before HealthReporter starts its background worker, following
  // the lifecycle cpp_supervised_app example.
  score::mw::log::rust::StdoutLoggerBuilder loggerBuilder;
  loggerBuilder.Context("TSIG")
      .LogLevel(score::mw::log::rust::LogLevel::Verbose)
      .SetAsDefaultLogger();

  if (!healthReporter_.initialize()) {
    std::cerr << "[TRAFFIC_SIGNAL_CONTROLLER][INIT][ERROR] "
                 "HealthReporter initialization failed\n";
    return EXIT_FAILURE;
  }

  cycleCount_ = 0U;
  initialized_ = true;
  std::cout << "[TRAFFIC_SIGNAL_CONTROLLER][INIT] application ready; "
               "period_ms=1000\n";
  return EXIT_SUCCESS;
}

std::int32_t SignalControlApplication::Run(
    const score::cpp::stop_token& stopToken) {
  if (!initialized_) {
    return EXIT_FAILURE;
  }

  using namespace std::chrono_literals;
  constexpr auto kPeriod = 1s;
  auto nextRelease = std::chrono::steady_clock::now() + kPeriod;
  std::int32_t exitCode{EXIT_SUCCESS};

  std::cout << "[TRAFFIC_SIGNAL_CONTROLLER][RUN] periodic loop started\n";
  while (!stopToken.stop_requested()) {
    // Delay the first release by one complete period so the heartbeat monitor
    // observes a real 1-second baseline. Absolute releases avoid timer drift.
    if (score::concurrency::wait_until(stopToken, nextRelease)) {
      break;
    }

    if (!healthReporter_.startControlCycle()) {   
      std::cerr << "[TRAFFIC_SIGNAL_CONTROLLER][RUN][ERROR] could not start "
                   "health-monitored control cycle\n";
      exitCode = EXIT_FAILURE;
      break;
    }

    // **TODO: ...............................................

    ++cycleCount_;
    std::cout << "[TRAFFIC_SIGNAL_CONTROLLER][CYCLE] hello; counter="
              << cycleCount_ << "; period_ms=1000\n";

    // Close the deadline after the useful control work. HealthMonitor combines
    // this result with the heartbeat and reports Alive asynchronously.
    healthReporter_.finishControlCycle();

    nextRelease += kPeriod;
  }

  healthReporter_.shutdown();
  initialized_ = false;
  std::cout << "[TRAFFIC_SIGNAL_CONTROLLER][STOP] cycles_completed="
            << cycleCount_ << '\n';
  return exitCode;
}

}  // namespace traffic_signal_controller
