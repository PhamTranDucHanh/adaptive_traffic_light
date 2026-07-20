#include "traffic_perception/traffic_perception_application.h"

#include <chrono>
#include <cstdlib>
#include <iostream>

#include "score/concurrency/interruptible_wait.h"
#include "score/mw/log/rust/stdout_logger_init.h"

namespace traffic_perception {

std::int32_t TrafficPerceptionApplication::Initialize(
    const score::mw::lifecycle::ApplicationContext& context) {
  (void)context;
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  // HealthMonitor logs through the S-CORE Rust logger. Initialize it before
  // starting HealthReporter's asynchronous worker, as in cpp_supervised_app.
  score::mw::log::rust::StdoutLoggerBuilder loggerBuilder;
  loggerBuilder.Context("TPER")
      .LogLevel(score::mw::log::rust::LogLevel::Verbose)
      .SetAsDefaultLogger();

  if (!healthReporter_.initialize()) {
    std::cerr << "[TRAFFIC_PERCEPTION][INIT][ERROR] HealthReporter "
                 "initialization failed\n";
    return EXIT_FAILURE;
  }

  cycleCount_ = 0U;
  initialized_ = true;
  std::cout << "[TRAFFIC_PERCEPTION][INIT] application ready; "
               "period_ms=3000\n";
  return EXIT_SUCCESS;
}

std::int32_t TrafficPerceptionApplication::Run(
    const score::cpp::stop_token& stopToken) {
  if (!initialized_) {
    return EXIT_FAILURE;
  }

  using namespace std::chrono_literals;
  constexpr auto kPeriod = 3s;
  auto nextRelease = std::chrono::steady_clock::now() + kPeriod;
  std::int32_t exitCode{EXIT_SUCCESS};

  std::cout << "[TRAFFIC_PERCEPTION][RUN] periodic loop started\n";
  while (!stopToken.stop_requested()) {
    // The first release occurs after one complete period, giving the heartbeat
    // monitor a real 3-second baseline. Absolute releases avoid timer drift.
    if (score::concurrency::wait_until(stopToken, nextRelease)) {
      break;
    }

    if (!healthReporter_.startPerceptionCycle()) {
      std::cerr << "[TRAFFIC_PERCEPTION][RUN][ERROR] could not start "
                   "health-monitored perception cycle\n";
      exitCode = EXIT_FAILURE;
      break;
    }

    // TODO: ................................................
    ++cycleCount_;
    std::cout << "[TRAFFIC_PERCEPTION][CYCLE] hello; counter=" << cycleCount_
              << "; period_ms=3000\n";

    healthReporter_.finishPerceptionCycle();
    nextRelease += kPeriod;
  }

  healthReporter_.shutdown();
  initialized_ = false;
  std::cout << "[TRAFFIC_PERCEPTION][STOP] cycles_completed=" << cycleCount_
            << '\n';
  return exitCode;
}

}  // namespace traffic_perception
