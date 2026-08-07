#ifndef TRAFFIC_PERCEPTION_INFERENCE_ONNX_THREAD_POOL_H_
#define TRAFFIC_PERCEPTION_INFERENCE_ONNX_THREAD_POOL_H_

#include <onnxruntime_cxx_api.h>

#include <array>
#include <atomic>
#include <cstddef>

namespace traffic_perception {

constexpr std::size_t kOrtIntraOpThreadCount{4};
constexpr std::size_t kOrtWorkerThreadCount{kOrtIntraOpThreadCount - 1};

// ONNX Runtime counts the thread calling Run() as one intra-op participant.
// This state therefore describes only the three additional ORT workers.
struct OrtThreadPoolConfig {
  explicit OrtThreadPoolConfig(int inferenceCallerCpu);

  std::array<int, kOrtWorkerThreadCount> workerCpus{};
  std::atomic<std::size_t> nextWorker{0};
};

// Installs pthread callbacks which apply scheduling and affinity attributes
// before each ONNX Runtime worker is created.
void ConfigureOrtThreadPool(Ort::SessionOptions& sessionOptions,
                            OrtThreadPoolConfig& threadPoolConfig);

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_INFERENCE_ONNX_THREAD_POOL_H_
