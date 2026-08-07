#include "traffic_perception/pipeline/pipeline_manager.h"

#include <chrono>
#include <thread>

#include "score/mw/log/logger.h"
#include "traffic_perception/core/time_utils.h"

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
    const auto nowNs = GetMonotonicTimeNs();
    const int64_t periodNs = period_.count() * 1000000LL;

    int64_t nextReleaseNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                nextRelease.time_since_epoch()).count();

    while (nextReleaseNs + periodNs <= nowNs) {
      nextReleaseNs += periodNs;
    }

    const int64_t expectedWakeup = nextReleaseNs;

    SleepUntilNs(expectedWakeup);

    const int64_t pipelineBegin = GetMonotonicTimeNs();

    runOneCycle(expectedWakeup, pipelineBegin);

    nextRelease = std::chrono::steady_clock::time_point(
        std::chrono::nanoseconds(nextReleaseNs)) + period_;
  }
}

void PipelineManager::stop() { running_ = false; }

void PipelineManager::runOneCycle(int64_t expectedWakeup,
                                   int64_t pipelineBegin) {
  static std::atomic<std::int64_t> cycleId{0};
  const std::int64_t currentCycle = ++cycleId;

  engine_.runOneCycle();

  const int64_t pipelineEnd = GetMonotonicTimeNs();

  getPipelineBenchmarkLogger().LogInfo()
      << "CycleId=" << currentCycle
      << " ExpectedWakeup=" << expectedWakeup
      << " Begin=" << pipelineBegin << " End=" << pipelineEnd;

  TrafficSnapshot snapshot = analyzer_.buildTrafficSnapshot();

  FrameContext ctx;
  ctx.timeline = analyzer_.latestTimeline();
  ctx.snapshot = std::move(snapshot);

  publisher_.broadcastSnapshot(ctx);
}

}  // namespace traffic_perception
