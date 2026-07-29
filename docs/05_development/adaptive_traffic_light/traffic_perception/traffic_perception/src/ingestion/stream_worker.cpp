#include "traffic_perception/ingestion/stream_worker.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>

#include "score/mw/log/logger.h"

namespace {

inline score::mw::log::Logger& getBenchmarkLogger() {
  static score::mw::log::Logger& logger =
      score::mw::log::CreateLogger("STRM", "Traffic Perception Stream");
  return logger;
}

}  // namespace

namespace traffic_perception {

bool StreamWorker::initStream(std::string sourceUri, int32_t streamId,
                              FramePool* pool,
                              std::chrono::milliseconds period,
                              std::chrono::milliseconds phase) {
  SourceUri = std::move(sourceUri);
  LaneId = streamId;
  Pool = pool;
  AcquisitionPeriod = period;
  phase_ = phase;

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

void StreamWorker::run(AtomicFrameBuffer& frameBuffer) {
  cv::VideoCapture cap = createCapture(SourceUri);

  if (!cap.isOpened()) {
    getBenchmarkLogger().LogError()
        << "[StreamWorker] Failed to open source " << SourceUri;
    return;
  }

  int32_t localFrameCounter = 0;

  auto nextRelease =
      std::chrono::steady_clock::now() + phase_;

  while (running_) {
    // Scheduled release time for this cycle.
    const auto scheduledRelease = nextRelease;

    // Wait until the next activation.
    std::this_thread::sleep_until(scheduledRelease);

    // Actual wakeup timestamp.
    const auto wakeupTime = std::chrono::steady_clock::now();

    const int64_t expectedWakeup =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            scheduledRelease.time_since_epoch())
            .count();

    // If we are more than one period late, drop backlog and
    // restart the periodic schedule from now.
    if (wakeupTime > scheduledRelease + AcquisitionPeriod) {
      nextRelease = wakeupTime + AcquisitionPeriod;
    } else {
      nextRelease = scheduledRelease + AcquisitionPeriod;
    }

    Frame* frame = Pool->acquire();
    if (frame == nullptr) {
      continue;
    }

    if (!cap.read(frame->Image)) {
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

    const int64_t captureBegin =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            wakeupTime.time_since_epoch())
            .count();

    frame->FrameId = ++localFrameCounter;
    frame->timeline.entries.clear();
    frame->timeline.frameId = frame->FrameId;
    frame->timeline.add(TimelineStage::Capture);

    Frame* old = frameBuffer.exchange(LaneId, frame);
    if (old != nullptr) {
      Pool->release(old);
    }

    const int64_t captureEnd =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();

    getBenchmarkLogger().LogInfo()
        << "LaneId=" << LaneId
        << " FrameId=" << frame->FrameId
        << " ExpectedWakeup=" << expectedWakeup
        << " Begin=" << captureBegin
        << " End=" << captureEnd;
  }

  cap.release();
}

}  // namespace traffic_perception
