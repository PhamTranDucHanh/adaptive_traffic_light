#include "traffic_perception/traffic_perception_application.h"

#include <sched.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>

#include "score/concurrency/interruptible_wait.h"
#include "score/mw/log/logging.h"
#include "score/mw/log/rust/stdout_logger_init.h"
#include "traffic_perception/core/runtime_paths.h"
#include "traffic_perception/core/types.h"

namespace {

constexpr std::int32_t kTrafficPerceptionMainCpu{1};
constexpr auto kSignalDisplayRefreshPeriod = std::chrono::milliseconds{250};

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

  // Pin only the lifecycle/viewer main thread. Stream, pipeline, and health
  // workers have already been created during Initialize(), so their existing
  // scheduling policy and CPU-affinity configuration remain unchanged.
  cpu_set_t affinityMask{};
  CPU_ZERO(&affinityMask);
  CPU_SET(kTrafficPerceptionMainCpu, &affinityMask);
  if (sched_setaffinity(0, sizeof(affinityMask), &affinityMask) != 0) {
    const std::int32_t affinityError = errno;
    std::cerr << "[TRAFFIC_PERCEPTION][RUN][CPU_AFFINITY][ERROR] "
                 "sched_setaffinity failed; cpu="
              << kTrafficPerceptionMainCpu << "; errno=" << affinityError
              << "; reason=" << std::strerror(affinityError) << '\n';
    shutdown();
    return EXIT_FAILURE;
  }
  score::mw::log::LogDebug()
      << "[TRAFFIC_PERCEPTION][RUN][CPU_AFFINITY] main thread pinned; cpu="
      << kTrafficPerceptionMainCpu << '\n';

  auto nextRelease = std::chrono::steady_clock::now() + viewerPhase_;
  auto nextContentRelease = nextRelease;
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
      if (scheduledRelease >= nextContentRelease) {
        if (!healthReporter_.startPerceptionCycle()) {
          std::cerr << "[TRAFFIC_PERCEPTION][RUN][ERROR] could not start "
                       "health-monitored perception cycle\n";
          exitCode = EXIT_FAILURE;
          break;
        }

        // Capture/inference frames remain on their configured, slower viewer
        // cadence. Signal countdown refreshes below never clone 1080p input.
        const auto previewFrames = perceptionModule_.latestPreviewFrames();
        viewer_.render(perceptionModule_.analyzer(), previewFrames,
                       toNanoseconds(scheduledRelease), toNanoseconds(wakeup));
        healthReporter_.finishPerceptionCycle();

        ++cycleCount_;
        score::mw::log::LogDebug()
            << "[TRAFFIC_PERCEPTION][CYCLE] viewer content rendered; counter="
            << cycleCount_ << "; period_ms=" << viewerPeriod_.count() << '\n';

        do {
          nextContentRelease += viewerPeriod_;
        } while (nextContentRelease <= scheduledRelease);
      } else {
        viewer_.refreshSignalOverlay(toNanoseconds(wakeup));
      }
      static_cast<void>(cv::waitKey(1));

      const auto releaseCheckTime = std::chrono::steady_clock::now();
      if (releaseCheckTime >
          scheduledRelease + kSignalDisplayRefreshPeriod) {
        nextRelease = releaseCheckTime + kSignalDisplayRefreshPeriod;
      } else {
        nextRelease = scheduledRelease + kSignalDisplayRefreshPeriod;
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
