#ifndef TRAFFIC_PERCEPTION_INFERENCE_ONNX_THREAD_POOL_H_
#define TRAFFIC_PERCEPTION_INFERENCE_ONNX_THREAD_POOL_H_

#include <onnxruntime_cxx_api.h>

#include <array>
#include <atomic>
#include <cstddef>

namespace traffic_perception {

// With one intra-op thread, inference executes entirely on the thread calling
// Session::Run(), which is the CPU-affined pipeline thread.
constexpr std::size_t kOrtIntraOpThreadCount{1};
constexpr std::size_t kOrtWorkerThreadCount{kOrtIntraOpThreadCount - 1};

// ONNX Runtime counts the thread calling Run() as one intra-op participant.
// This state describes only additional workers (none in the current profile).
struct OrtThreadPoolConfig {
  explicit OrtThreadPoolConfig(int inferenceCallerCpu,
                               int inferenceCallerPriority);

  std::array<int, kOrtWorkerThreadCount> workerCpus{};
  std::atomic<std::size_t> nextWorker{0};
  int workerPriority{1};
};

// Installs pthread callbacks which apply scheduling and affinity attributes
// before each ONNX Runtime worker is created.
void ConfigureOrtThreadPool(Ort::SessionOptions& sessionOptions,
                            OrtThreadPoolConfig& threadPoolConfig);

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_INFERENCE_ONNX_THREAD_POOL_H_
