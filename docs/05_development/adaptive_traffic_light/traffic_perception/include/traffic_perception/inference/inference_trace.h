#ifndef TRAFFIC_PERCEPTION_INFERENCE_INFERENCE_TRACE_H_
#define TRAFFIC_PERCEPTION_INFERENCE_INFERENCE_TRACE_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "score/mw/log/logging.h"

namespace traffic_perception {

using InferenceTraceClock = std::chrono::steady_clock;

inline std::uint64_t InferenceTraceCallLimit() {
  static const std::uint64_t limit = [] {
    const char* value = std::getenv("TRAFFIC_AI_TRACE_FIRST_N");
    if (value == nullptr || *value == '\0') return std::uint64_t{0};
    char* end = nullptr;
    const auto parsed = std::strtoull(value, &end, 10);
    return end != value && *end == '\0' ? static_cast<std::uint64_t>(parsed)
                                        : std::uint64_t{0};
  }();
  return limit;
}

inline std::int64_t InferenceTraceElapsedUs(
    const InferenceTraceClock::time_point begin,
    const InferenceTraceClock::time_point end) {
  return std::chrono::duration_cast<std::chrono::microseconds>(end - begin)
      .count();
}

inline std::int64_t InferenceTraceTimestampNs(
    const InferenceTraceClock::time_point timestamp) {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             timestamp.time_since_epoch())
      .count();
}

inline void LogInferenceTrace(
    const char* backend, const std::uint64_t callIndex,
    const std::int32_t frameId, const InferenceTraceClock::time_point begin,
    const InferenceTraceClock::time_point preprocessEnd,
    const InferenceTraceClock::time_point runtimeEnd,
    const InferenceTraceClock::time_point end, const std::size_t detections) {
  if (callIndex >= InferenceTraceCallLimit()) return;
  score::mw::log::LogInfo()
      << "[AI_TRACE] backend=" << backend << " call=" << callIndex
      << " kind=" << (frameId == 0 ? "warmup" : "live")
      << " frame_id=" << frameId << " status=ok"
      << " preprocess_us=" << InferenceTraceElapsedUs(begin, preprocessEnd)
      << " runtime_us=" << InferenceTraceElapsedUs(preprocessEnd, runtimeEnd)
      << " postprocess_us=" << InferenceTraceElapsedUs(runtimeEnd, end)
      << " total_us=" << InferenceTraceElapsedUs(begin, end)
      << " begin_ns=" << InferenceTraceTimestampNs(begin)
      << " preprocess_end_ns=" << InferenceTraceTimestampNs(preprocessEnd)
      << " runtime_end_ns=" << InferenceTraceTimestampNs(runtimeEnd)
      << " end_ns=" << InferenceTraceTimestampNs(end)
      << " detections=" << detections;
}

inline void LogInferenceTraceFailure(
    const char* backend, const std::uint64_t callIndex,
    const std::int32_t frameId, const char* failedStage,
    const InferenceTraceClock::time_point begin,
    const InferenceTraceClock::time_point preprocessEnd,
    const InferenceTraceClock::time_point runtimeEnd,
    const InferenceTraceClock::time_point end) {
  if (callIndex >= InferenceTraceCallLimit()) return;
  score::mw::log::LogInfo()
      << "[AI_TRACE] backend=" << backend << " call=" << callIndex
      << " kind=" << (frameId == 0 ? "warmup" : "live")
      << " frame_id=" << frameId << " status=error"
      << " failed_stage=" << failedStage
      << " preprocess_us=" << InferenceTraceElapsedUs(begin, preprocessEnd)
      << " runtime_us=" << InferenceTraceElapsedUs(preprocessEnd, runtimeEnd)
      << " total_us=" << InferenceTraceElapsedUs(begin, end)
      << " begin_ns=" << InferenceTraceTimestampNs(begin)
      << " preprocess_end_ns=" << InferenceTraceTimestampNs(preprocessEnd)
      << " runtime_end_ns=" << InferenceTraceTimestampNs(runtimeEnd)
      << " end_ns=" << InferenceTraceTimestampNs(end);
}

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_INFERENCE_INFERENCE_TRACE_H_
