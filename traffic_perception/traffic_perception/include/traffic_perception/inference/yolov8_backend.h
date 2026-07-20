#ifndef TRAFFIC_PERCEPTION_INFERENCE_YOLOV8_BACKEND_H_
#define TRAFFIC_PERCEPTION_INFERENCE_YOLOV8_BACKEND_H_

#include <opencv2/core.hpp>
#include <string>
#include <vector>
#include <memory>
#include <onnxruntime_cxx_api.h>
#include "traffic_perception/inference/imodel_backend.h"

namespace traffic_perception {

class YOLOv8Backend : public IModelBackend {
 public:
  explicit YOLOv8Backend(const std::string& modelPath);
  ~YOLOv8Backend() override = default;

  InferenceResult infer(const Frame& frame) override;
  std::string getModelName() const override;
  void draw(cv::Mat& image,
            const InferenceResult& inference) override;

 private:
  Ort::Env env_;
  std::unique_ptr<Ort::Session> session_;
  Ort::MemoryInfo memory_info_;
  
  std::vector<char*> input_node_names_;
  std::vector<char*> output_node_names_;

  float confThreshold_ = 0.5f;
  float nmsThreshold_ = 0.4f;

  void preprocess(const cv::Mat& frame, std::vector<float>& input_tensor_values);
  void postprocess(const cv::Mat& frame, const std::vector<Ort::Value>& output_tensors, InferenceResult& result);
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_INFERENCE_YOLOV8_BACKEND_H_
