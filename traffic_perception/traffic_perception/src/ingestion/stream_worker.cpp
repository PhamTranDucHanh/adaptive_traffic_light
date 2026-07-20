#include "traffic_perception/ingestion/stream_worker.h"

#include <iostream>
#include <chrono>
#include <algorithm>
#include <cctype>

namespace traffic_perception {

StreamWorker::~StreamWorker() {
  stop();
}

bool StreamWorker::initStream(std::string sourceUri, int32_t streamId, FramePool* pool, std::chrono::milliseconds period) {
  SourceUri = std::move(sourceUri);
  LaneId = streamId;
  Pool = pool;
  AcquisitionPeriod = period;
  std::cout << "[StreamWorker] initStream() for Lane " << LaneId << " with " << SourceUri << " at period " << AcquisitionPeriod.count() << "ms\n";
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
  InputSourceType type = detectSourceType(source);

  switch (type) {
    case InputSourceType::RtspStream:
      return cv::VideoCapture(source, cv::CAP_GSTREAMER);
    case InputSourceType::Camera:
      return cv::VideoCapture(std::stoi(source), cv::CAP_ANY);
    case InputSourceType::LocalFile:
    default:
      // Force FFMPEG backend for local files to avoid GStreamer pipeline issues
      return cv::VideoCapture(source, cv::CAP_FFMPEG);
  }
}
void StreamWorker::start(AtomicFrameBuffer &frameBuffer) {
  Running = true;
  WorkerThread = std::thread(&StreamWorker::producerLoop, this, std::ref(frameBuffer));
}

void StreamWorker::stop() {
  Running = false;
  if (WorkerThread.joinable()) {
    WorkerThread.join();
  }
}

void StreamWorker::producerLoop(AtomicFrameBuffer &frameBuffer) {
  InputSourceType type = detectSourceType(SourceUri);
  std::cout << "[StreamWorker] Detected source type: " << static_cast<int>(type) << " for " << SourceUri << '\n';

  cv::VideoCapture cap = createCapture(SourceUri);

  if (!cap.isOpened()) {
    std::cerr << "[StreamWorker] Failed to open " << SourceUri << '\n';
    return;
  }
  
  int32_t localFrameCounter = 0;
  auto nextRelease = std::chrono::steady_clock::now();

  // The worker is periodic by design. 
  // The AcquisitionPeriod bounds CPU utilization and allows synchronization 
  // with the perception pipeline, independent of input video FPS.
  while (Running) {
    nextRelease += AcquisitionPeriod;

    Frame* frame = Pool->acquire();
    if (frame != nullptr) {
      if (cap.read(frame->Image)) {
        frame->FrameId = ++localFrameCounter;
        std::cout << "[StreamWorker] Published FrameId: " << frame->FrameId << " Lane: " << LaneId << std::endl;
        Frame* old = frameBuffer.exchange(LaneId, frame);
        if (old != nullptr) {
            Pool->release(old);
        }
      } else {
        // Loop on EOF for files
        if (detectSourceType(SourceUri) == InputSourceType::LocalFile) {
          cap.set(cv::CAP_PROP_POS_FRAMES, 0);
        }
        Pool->release(frame);
      }
    }

    std::this_thread::sleep_until(nextRelease);
  }
  cap.release();
}

int32_t StreamWorker::getHealthStatus() const {
  return Running ? 1 : 0;
}

}  // namespace traffic_perception

