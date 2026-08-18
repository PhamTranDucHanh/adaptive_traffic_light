#ifndef TRAFFIC_PERCEPTION_INFERENCE_RT_DETRV2_BACKEND_H_
#define TRAFFIC_PERCEPTION_INFERENCE_RT_DETRV2_BACKEND_H_

#include <onnxruntime_cxx_api.h>

#include <cstdint>
#include <memory>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

#include "traffic_perception/inference/imodel_backend.h"
#include "traffic_perception/inference/onnx_thread_pool.h"

namespace traffic_perception {

// Backend for official RT-DETRv2 deployment exports. Model capacity (S/L/X)
// does not affect this interface: tensor dimensions and query count are read
// from the loaded ONNX session at runtime.
class RtDetrv2Backend final : public IModelBackend {
 public:
  RtDetrv2Backend(const std::string& modelPath,
                  const std::string& emergencyClass, int inferenceCallerCpu = 2,
                  int inferenceCallerPriority = 70);
  ~RtDetrv2Backend() override = default;

  InferenceResult infer(const Frame& frame) override;
  std::string getModelName() const override;
  void draw(cv::Mat& image, const InferenceResult& inference) override;

 private:
  void inspectModelContract();
  void preprocess(const cv::Mat& frame, std::vector<float>& tensor);
  void postprocess(const cv::Mat& frame, const std::vector<Ort::Value>& outputs,
                   InferenceResult& result) const;

  bool isVehicleClass(std::int64_t classId) const;
  bool isEmergencyClass(std::int64_t classId) const;

  std::string emergencyClass_;
  Ort::Env env_;
  OrtThreadPoolConfig ortThreadPoolConfig_;
  std::unique_ptr<Ort::Session> session_;
  Ort::MemoryInfo memoryInfo_;

  std::vector<std::string> inputNamesStorage_;
  std::vector<std::string> outputNamesStorage_;
  std::vector<const char*> inputNames_;
  std::vector<const char*> outputNames_;

  std::int64_t inputHeight_{640};
  std::int64_t inputWidth_{640};
  std::vector<float> inputTensorValues_;
  cv::Mat resizedBuffer_;
  cv::Mat rgbBuffer_;
  cv::Mat normalizedBuffer_;
  std::size_t labelsOutputIndex_{0};
  std::size_t boxesOutputIndex_{1};
  std::size_t scoresOutputIndex_{2};
  float confidenceThreshold_{0.5F};
  std::uint64_t inferenceCallCount_{0};
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_INFERENCE_RT_DETRV2_BACKEND_H_
