#include "traffic_perception/ingestion/stream_worker.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <thread>
#include <pthread.h>

#include <unistd.h>

#include "score/mw/log/logger.h"
#include "traffic_perception/core/time_utils.h"

namespace {

inline score::mw::log::Logger& getBenchmarkLogger() {
  static score::mw::log::Logger& logger =
      score::mw::log::CreateLogger("STRM", "Traffic Perception Stream");
  return logger;
}

}  // namespace

namespace traffic_perception {

bool StreamWorker::initStream(std::string sourceUri, int32_t streamId,
                              FramePool* pool, std::chrono::milliseconds period,
                              std::chrono::milliseconds phase,
                              int32_t decodeCore) {
  SourceUri = std::move(sourceUri);
  LaneId = streamId;
  Pool = pool;
  AcquisitionPeriod = period;
  phase_ = phase;
  DecodeCore = decodeCore;

  getBenchmarkLogger().LogDebug()
      << "[StreamWorker] Init lane " << LaneId << " source=" << SourceUri
      << " period=" << AcquisitionPeriod.count() << "ms";

  return true;
}

InputSourceType StreamWorker::detectSourceType(const std::string& source) {
  if (source.rfind("rtsp://", 0) == 0 || source.rfind("rtsps://", 0) == 0) {
    return InputSourceType::RtspStream;
  }

  if (std::all_of(source.begin(), source.end(), ::isdigit)) {
    return InputSourceType::Camera;
  }

  return InputSourceType::LocalFile;
}

cv::VideoCapture StreamWorker::createCapture(const std::string& source) {
  switch (detectSourceType(source)) {
    case InputSourceType::RtspStream:
      return cv::VideoCapture(source, cv::CAP_GSTREAMER);

    case InputSourceType::Camera:
      return cv::VideoCapture(std::stoi(source), cv::CAP_ANY);

    case InputSourceType::LocalFile:
    default:
      return cv::VideoCapture(source, cv::CAP_FFMPEG);
  }
}

void StreamWorker::stop() { running_.store(false, std::memory_order_relaxed); }

void StreamWorker::run(AtomicFrameBuffer& frameBuffer,
                       std::chrono::steady_clock::time_point startTime) {
  cv::VideoCapture cap;

  struct ThreadTask {
    std::function<void()> func;
  };

  ThreadTask task;
  task.func = [&]() {
    setenv("OPENCV_FFMPEG_THREADS", "1", 1);
    cap = createCapture(SourceUri);
  };

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  // Decoder helpers inherit SCHED_OTHER and this stream's configured core.
  pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  struct sched_param sp {};
  sp.sched_priority = 0;
  pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
  pthread_attr_setschedparam(&attr, &sp);

  if (DecodeCore >= 0) {
    cpu_set_t affinityMask{};
    CPU_ZERO(&affinityMask);
    CPU_SET(DecodeCore, &affinityMask);
    pthread_attr_setaffinity_np(&attr, sizeof(affinityMask), &affinityMask);
  }

  pthread_t tid;
  pthread_create(&tid, &attr, [](void* arg) -> void* {
    auto* t = static_cast<ThreadTask*>(arg);
    t->func();
    return nullptr;
  }, &task);

  pthread_join(tid, nullptr);
  pthread_attr_destroy(&attr);

  if (!cap.isOpened()) {
    getBenchmarkLogger().LogError()
        << "[StreamWorker] Failed to open source " << SourceUri;
    return;
  }

  int32_t localFrameCounter = 0;

  const auto lanePhase =
      AcquisitionPeriod * static_cast<std::int64_t>(LaneId) /
      static_cast<std::int64_t>(NUM_LANES);

  auto nextRelease = startTime + phase_ + lanePhase;

  while (running_) {
    const auto nowNs = GetMonotonicTimeNs();
    const int64_t periodNs = AcquisitionPeriod.count() * 1000000LL;

    int64_t nextReleaseNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                nextRelease.time_since_epoch()).count();

    while (nextReleaseNs + periodNs <= nowNs) {
      nextReleaseNs += periodNs;
    }

    const int64_t expectedWakeup = nextReleaseNs;

    SleepUntilNs(expectedWakeup);

    const int64_t captureBegin = GetMonotonicTimeNs();

    // Advance to the next nominal release time.
    nextRelease = std::chrono::steady_clock::time_point(
        std::chrono::nanoseconds(nextReleaseNs)) + AcquisitionPeriod;

    const auto acquireBegin = std::chrono::steady_clock::now();

    Frame* frame = Pool->acquire();

    const auto acquireEnd = std::chrono::steady_clock::now();

    if (frame == nullptr) {
      continue;
    }

    const auto grabBegin = std::chrono::steady_clock::now();

    if (!cap.grab()) {
      if (detectSourceType(SourceUri) == InputSourceType::LocalFile) {
        cap.set(cv::CAP_PROP_POS_FRAMES, 0);
        Pool->release(frame);
        continue;
      }

      getBenchmarkLogger().LogError()
          << "[StreamWorker] Stream lost on lane " << LaneId;

      Pool->release(frame);
      break;
    }

    const auto grabEnd = std::chrono::steady_clock::now();

    const auto decodeBegin = std::chrono::steady_clock::now();

    if (!cap.retrieve(frame->Image)) {
      Pool->release(frame);
      continue;
    }

    {
      // cv::Mat assignment only increments the reference count. This keeps a
      // display reference without copying a 1080p image every capture cycle.
      std::lock_guard<std::mutex> lock(previewMutex_);
      latestPreviewFrame_ = frame->Image;
    }

    const auto decodeEnd = std::chrono::steady_clock::now();

    const int64_t acquireBeginNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            acquireBegin.time_since_epoch())
            .count();

    const int64_t acquireEndNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            acquireEnd.time_since_epoch())
            .count();

    const int64_t grabBeginNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            grabBegin.time_since_epoch())
            .count();

    const int64_t grabEndNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            grabEnd.time_since_epoch())
            .count();

    const int64_t decodeBeginNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            decodeBegin.time_since_epoch())
            .count();

    const int64_t decodeEndNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            decodeEnd.time_since_epoch())
            .count();

    frame->FrameId = ++localFrameCounter;
    frame->timeline.entries.clear();
    frame->timeline.frameId = frame->FrameId;
    frame->timeline.add(TimelineStage::Capture);

    Frame* old = frameBuffer.exchange(LaneId, frame);
    if (old != nullptr) {
      Pool->release(old);
    }

    const int64_t captureEnd = GetMonotonicTimeNs();

    getBenchmarkLogger().LogInfo()
        << "LaneId=" << LaneId << " FrameId=" << frame->FrameId
        << " ExpectedWakeup=" << expectedWakeup
        << " Begin=" << captureBegin
        << " AcquireBegin=" << acquireBeginNs
        << " AcquireEnd=" << acquireEndNs
        << " GrabBegin=" << grabBeginNs
        << " GrabEnd=" << grabEndNs
        << " DecodeBegin=" << decodeBeginNs
        << " DecodeEnd=" << decodeEndNs
        << " End=" << captureEnd;
  }

  cap.release();
}

std::int32_t StreamWorker::getLaneId() const {
  return LaneId;
}

cv::Mat StreamWorker::latestPreviewFrame() const {
  std::lock_guard<std::mutex> lock(previewMutex_);
  return latestPreviewFrame_.clone();
}

}  // namespace traffic_perception
