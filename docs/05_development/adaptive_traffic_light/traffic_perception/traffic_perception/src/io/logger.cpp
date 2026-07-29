#include "traffic_perception/io/logger.h"

#include <chrono>
#include <sstream>

#include "score/mw/log/logger.h"

namespace traffic_perception {

namespace {

inline score::mw::log::Logger& timelineLogger() noexcept {
  static auto& logger =
      score::mw::log::CreateLogger("TPER_TIMELINE",
                                   "Traffic Perception Timeline");
  return logger;
}

}  // namespace

void Logger::log(const Timeline& timeline) {
  buffer_[head_] = timeline;
  head_ = (head_ + 1) % CAPACITY;

  if (size_ < CAPACITY) {
    ++size_;
  } else {
    tail_ = (tail_ + 1) % CAPACITY;
  }
}

void Logger::dump() {
  if (size_ == 0) {
    return;
  }

  std::ostringstream oss;
  std::size_t idx = tail_;
  for (std::size_t i = 0; i < size_; ++i) {
    const auto& timeline = buffer_[idx];
    int64_t capture = -1, inferenceBegin = -1, inferenceEnd = -1,
            analyzerBegin = -1, analyzerEnd = -1, publish = -1;
    if (!timeline.entries.empty()) {
      for (const auto& entry : timeline.entries) {
        // Absolute timestamp (nanoseconds) since epoch
        int64_t absolute = entry.timestamp.time_since_epoch().count();
        std::string stageStr = to_string(entry.stage);
        if (stageStr == "Capture") {
          capture = absolute;
        } else if (stageStr == "InferenceBegin") {
          inferenceBegin = absolute;
        } else if (stageStr == "InferenceEnd") {
          inferenceEnd = absolute;
        } else if (stageStr == "AnalyzerBegin") {
          analyzerBegin = absolute;
        } else if (stageStr == "AnalyzerEnd") {
          analyzerEnd = absolute;
        } else if (stageStr == "Publish") {
          publish = absolute;
        }
      }
    }
    oss << "FrameId=" << timeline.frameId
        << " Capture=" << capture
        << " InferenceBegin=" << inferenceBegin
        << " InferenceEnd=" << inferenceEnd
        << " AnalyzerBegin=" << analyzerBegin
        << " AnalyzerEnd=" << analyzerEnd
        << " Publish=" << publish << '\n';
    idx = (idx + 1) % CAPACITY;
  }

  timelineLogger().LogInfo() << oss.str();

  clear();
}


void Logger::clear() {
  head_ = 0;
  tail_ = 0;
  size_ = 0;
}

}  // namespace traffic_perception
