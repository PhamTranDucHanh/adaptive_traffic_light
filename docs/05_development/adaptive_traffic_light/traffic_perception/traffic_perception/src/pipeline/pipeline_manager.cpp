#include "traffic_perception/pipeline/pipeline_manager.h"

#include <chrono>
#include <thread>

#include "score/mw/log/logger.h"

namespace {

inline score::mw::log::Logger& getPipelineBenchmarkLogger() {
  static score::mw::log::Logger& logger =
      score::mw::log::CreateLogger("PIPE", "Traffic Perception Pipeline");
  return logger;
}

}  // namespace

namespace traffic_perception {

PipelineManager::PipelineManager(IModelBackend& backend,
                                 AtomicFrameBuffer& buffer, FramePool& pool,
                                 const std::array<Roi, NUM_LANES>& laneRois,
                                 std::chrono::milliseconds period,
                                 std::chrono::milliseconds phase)
    : period_(period),
      phase_(phase),
      laneRois_(laneRois),
      analyzer_(pool, laneRois_),
      publisher_(),
      engine_(backend, buffer, pool, analyzer_, laneRois_) {}

void PipelineManager::run(std::chrono::steady_clock::time_point startTime) {
  running_ = true;

  auto nextRelease = startTime + phase_;

  while (running_) {
    // If we are more than one period late, drop backlog and
    // restart the schedule from now.
    const auto now = std::chrono::steady_clock::now();
    if (now > nextRelease + period_) {
      nextRelease = now + period_;
    }

    // Scheduled release time for this cycle.
    const auto scheduledRelease = nextRelease;

    // Wait until the next activation.
    std::this_thread::sleep_until(scheduledRelease);

    // Actual wakeup time.
    const auto wakeup = std::chrono::steady_clock::now();

    const int64_t expectedWakeup =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            scheduledRelease.time_since_epoch())
            .count();

    const int64_t pipelineBegin =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            wakeup.time_since_epoch())
            .count();

    runOneCycle(expectedWakeup, pipelineBegin);

    nextRelease = scheduledRelease + period_;
  }
}

void PipelineManager::stop() { running_ = false; }

void PipelineManager::runOneCycle(int64_t expectedWakeup,
                                  int64_t pipelineBegin) {
  engine_.runOneCycle();

  const int64_t pipelineEnd =
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count();

  getPipelineBenchmarkLogger().LogInfo()
      << "ExpectedWakeup=" << expectedWakeup << " Begin=" << pipelineBegin
      << " End=" << pipelineEnd;

  TrafficSnapshot snapshot = analyzer_.buildTrafficSnapshot();

  FrameContext ctx;
  ctx.frame = analyzer_.latestFrame();
  ctx.timeline = analyzer_.latestTimeline();
  ctx.snapshot = std::move(snapshot);

  publisher_.broadcastSnapshot(ctx);
}

}  // namespace traffic_perception
