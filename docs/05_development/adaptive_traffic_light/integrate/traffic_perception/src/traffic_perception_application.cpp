#include "traffic_perception/traffic_perception_application.h"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>

#include "score/concurrency/interruptible_wait.h"
#include "score/mw/log/logging.h"
#include "score/mw/log/rust/stdout_logger_init.h"
#include "traffic_perception/core/runtime_paths.h"
#include "traffic_perception/core/types.h"

namespace {

std::int64_t toNanoseconds(
    const std::chrono::steady_clock::time_point timestamp) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             timestamp.time_since_epoch())
      .count();
}

}  // namespace

namespace traffic_perception {

std::int32_t TrafficPerceptionApplication::Initialize(
    const score::mw::lifecycle::ApplicationContext& context) {
  (void)context;

  score::mw::log::LogDebug() << std::unitbuf;
  std::cerr << std::unitbuf;

  // HealthMonitor logs through the S-CORE Rust logger. Initialize it before
  // starting HealthReporter's asynchronous worker, as in cpp_supervised_app.
  score::mw::log::rust::StdoutLoggerBuilder loggerBuilder;
  loggerBuilder.Context("TPER")
      .LogLevel(score::mw::log::rust::LogLevel::Verbose)
      .SetAsDefaultLogger();

  try {
    const std::string configPath =
        (RuntimePaths::Etc() / "traffic_perception_config.json").string();
    configManager_ = ConfigManager(configPath);
    if (!configManager_.loadConfig()) {
      return EXIT_FAILURE;
    }

    // Resolve packaged model/video paths exactly as the successful standalone
    // pipeline test resolves its Bazel runfiles.
    AppConfig& config = configManager_.getConfig();
    config.modelPath =
        (RuntimePaths::Models() /
         std::filesystem::path(config.modelPath).filename())
            .string();
    for (std::size_t i = 0; i < NUM_LANES; ++i) {
      config.lanes[i].videoSource =
          (RuntimePaths::Etc() /
           std::filesystem::path(config.lanes[i].videoSource).filename())
              .string();
    }

    if (config.ViewerPeriod <= std::chrono::milliseconds::zero() ||
        config.ViewerPhase < std::chrono::milliseconds::zero()) {
      std::cerr << "[TRAFFIC_PERCEPTION][INIT][ERROR] invalid viewer timing\n";
      return EXIT_FAILURE;
    }

    // PerceptionModule is the tested owner of the pool, backend, capture
    // workers and pipeline thread. Do not duplicate that ownership here.
    if (!perceptionModule_.initModule(config) ||
        !perceptionModule_.startThreads()) {
      std::cerr << "[TRAFFIC_PERCEPTION][INIT][ERROR] perception module "
                   "initialization failed\n";
      perceptionModule_.stopThreads();
      return EXIT_FAILURE;
    }
    perceptionStarted_ = true;

    if (!viewer_.init(config, perceptionModule_.backend())) {
      std::cerr
          << "[TRAFFIC_PERCEPTION][INIT][ERROR] viewer initialization failed\n";
      shutdown();
      return EXIT_FAILURE;
    }
    viewerInitialized_ = true;
    viewerPeriod_ = config.ViewerPeriod;
    viewerPhase_ = config.ViewerPhase;

    // HealthMonitor must be running before Initialize() returns because
    // run_application reports the managed process as Running immediately
    // afterwards.
    if (!healthReporter_.initialize()) {
      std::cerr << "[TRAFFIC_PERCEPTION][INIT][ERROR] HealthReporter "
                   "initialization failed\n";
      shutdown();
      return EXIT_FAILURE;
    }
  } catch (const std::exception& error) {
    std::cerr << "[TRAFFIC_PERCEPTION][INIT][ERROR] exception: " << error.what()
              << '\n';
    shutdown();
    return EXIT_FAILURE;
  }

  cycleCount_ = 0U;
  initialized_ = true;
  score::mw::log::LogDebug()
      << "[TRAFFIC_PERCEPTION][INIT] application ready; viewer_period_ms="
      << viewerPeriod_.count() << "; viewer_phase_ms=" << viewerPhase_.count()
      << "; lifecycle_supervision_window_ms=10000\n";
  return EXIT_SUCCESS;
}

std::int32_t TrafficPerceptionApplication::Run(
    const score::cpp::stop_token& stopToken) {
  if (!initialized_) {
    return EXIT_FAILURE;
  }

  auto nextRelease = std::chrono::steady_clock::now() + viewerPhase_;
  std::int32_t exitCode{EXIT_SUCCESS};

  score::mw::log::LogDebug()
      << "[TRAFFIC_PERCEPTION][RUN] periodic viewer loop started; "
         "release_clock=steady_clock; schedule=absolute\n";
  try {
    while (!stopToken.stop_requested()) {
      const auto scheduledRelease = nextRelease;
      if (score::concurrency::wait_until(stopToken, scheduledRelease)) {
        break;
      }

      const auto wakeup = std::chrono::steady_clock::now();
      if (!healthReporter_.startPerceptionCycle()) {
        std::cerr << "[TRAFFIC_PERCEPTION][RUN][ERROR] could not start "
                     "health-monitored perception cycle\n";
        exitCode = EXIT_FAILURE;
        break;
      }

      // The capture and inference pipeline is already running in the tested
      // PerceptionModule threads. The lifecycle thread owns only the periodic
      // viewer work, matching pipeline_manager_test.
      const auto previewFrames = perceptionModule_.latestPreviewFrames();
      viewer_.render(perceptionModule_.analyzer(), previewFrames,
                     toNanoseconds(scheduledRelease), toNanoseconds(wakeup));
      static_cast<void>(cv::waitKey(1));
      healthReporter_.finishPerceptionCycle();

      ++cycleCount_;
      score::mw::log::LogDebug()
          << "[TRAFFIC_PERCEPTION][CYCLE] viewer rendered; counter="
          << cycleCount_ << "; period_ms=" << viewerPeriod_.count() << '\n';

      // Preserve the test's absolute schedule and discard a stale backlog if
      // the thread woke more than one complete period late.
      if (wakeup > scheduledRelease + viewerPeriod_) {
        nextRelease = wakeup + viewerPeriod_;
      } else {
        nextRelease = scheduledRelease + viewerPeriod_;
      }
    }
  } catch (const std::exception& error) {
    std::cerr << "[TRAFFIC_PERCEPTION][RUN][ERROR] exception: " << error.what()
              << '\n';
    exitCode = EXIT_FAILURE;
  }

  shutdown();
  score::mw::log::LogDebug()
      << "[TRAFFIC_PERCEPTION][STOP] cycles_completed=" << cycleCount_ << '\n';
  score::mw::log::LogDebug()
      << "[TRAFFIC_PERCEPTION][STOP] exit_code=" << exitCode;
  return exitCode;
}

void TrafficPerceptionApplication::shutdown() {
  if (viewerInitialized_) {
    viewer_.shutdown();
    viewerInitialized_ = false;
  }
  if (perceptionStarted_) {
    perceptionModule_.stopThreads();
    perceptionStarted_ = false;
  }
  healthReporter_.shutdown();
  initialized_ = false;
}

}  // namespace traffic_perception
