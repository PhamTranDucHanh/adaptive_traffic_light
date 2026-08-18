#ifndef TRAFFIC_PERCEPTION_INFERENCE_YOLOV8_BACKEND_H_
#define TRAFFIC_PERCEPTION_INFERENCE_YOLOV8_BACKEND_H_

#include <onnxruntime_cxx_api.h>

#include <cstdint>
#include <memory>
#include <opencv2/core.hpp>
#include <string>
#include <vector>

#include "traffic_perception/inference/imodel_backend.h"
#include "traffic_perception/inference/onnx_thread_pool.h"

namespace traffic_perception {

class YOLOv8Backend : public IModelBackend {
 public:
  YOLOv8Backend(const std::string& modelPath,
                const std::string& emergencyClass,
                int inferenceCallerCpu = 2);
  ~YOLOv8Backend() override = default;

  InferenceResult infer(const Frame& frame) override;
  std::string getModelName() const override;
  void draw(cv::Mat& image, const InferenceResult& inference) override;
  void draw(cv::Mat& image, const std::vector<Detection>& detections);

 private:
  std::string emergencyClass_;

  // COCO class ID mapping for semantic classification.
  // Only IDs relevant to traffic perception are listed.
  enum class CocoClass : int {
    Person = 0,
    Bicycle = 1,
    Car = 2,
    Motorcycle = 3,
    Airplane = 4,
    Bus = 5,
    Train = 6,
    Truck = 7,
    Boat = 8,
  };

  Ort::Env env_;
  OrtThreadPoolConfig ortThreadPoolConfig_;
  std::unique_ptr<Ort::Session> session_;
  Ort::MemoryInfo memory_info_;

  std::vector<char*> input_node_names_;
  std::vector<char*> output_node_names_;

  float confThreshold_ = 0.5f;
  float nmsThreshold_ = 0.4f;
  mutable int frameCount_ = 0;
  std::uint64_t inferenceCallCount_{0};

  float ratio_{1.0f};
  float dw_{0.0f};
  float dh_{0.0f};

  void preprocess(const cv::Mat& frame,
                  std::vector<float>& input_tensor_values);
  void postprocess(const cv::Mat& frame,
                   const std::vector<Ort::Value>& output_tensors,
                   InferenceResult& result);

  bool isVehicleClass(int classId) const;
  bool isEmergencyClass(int classId) const;

  static Detection populateDetection(const cv::Rect& box, int classId,
                                     float confidence, bool isVehicle,
                                     bool isEmergency);
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_INFERENCE_YOLOV8_BACKEND_H_
