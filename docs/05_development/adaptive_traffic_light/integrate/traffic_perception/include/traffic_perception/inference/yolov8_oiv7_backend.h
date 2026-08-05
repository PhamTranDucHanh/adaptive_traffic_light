#ifndef TRAFFIC_PERCEPTION_INFERENCE_YOLOV8_OIV7_BACKEND_H_
#define TRAFFIC_PERCEPTION_INFERENCE_YOLOV8_OIV7_BACKEND_H_

#include <opencv2/core.hpp>
#include <string>
#include <vector>
#include <memory>
#include <onnxruntime_cxx_api.h>
#include "traffic_perception/inference/imodel_backend.h"

namespace traffic_perception {

class YoloV8OIV7Backend : public IModelBackend {
 public:
  YoloV8OIV7Backend(const std::string& modelPath,
                    const std::string& emergencyClass);
  ~YoloV8OIV7Backend() override = default;

  InferenceResult infer(const Frame& frame) override;
  std::string getModelName() const override;
  void draw(cv::Mat& image,
            const InferenceResult& inference) override;

 private:
  std::string emergencyClass_;

  Ort::Env env_;
  std::unique_ptr<Ort::Session> session_;
  Ort::MemoryInfo memory_info_;

  std::vector<char*> input_node_names_;
  std::vector<char*> output_node_names_;

  float confThreshold_ = 0.2f;
  float nmsThreshold_ = 0.4f;
  mutable int frameCount_ = 0;

  float ratio_{1.0f};
  float dw_{0.0f};
  float dh_{0.0f};

  void preprocess(const cv::Mat& frame, std::vector<float>& input_tensor_values);
  void postprocess(const cv::Mat& frame, const std::vector<Ort::Value>& output_tensors, InferenceResult& result);

  bool isVehicleClass(const std::string& className) const;
  bool isEmergencyClass(const std::string& className) const;

  static Detection populateDetection(const cv::Rect& box, int classId,
                                     const std::string& className,
                                     float confidence, bool isVehicle,
                                     bool isEmergency);
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_INFERENCE_YOLOV8_OIV7_BACKEND_H_
