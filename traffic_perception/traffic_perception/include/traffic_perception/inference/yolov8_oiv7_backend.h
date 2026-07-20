#ifndef TRAFFIC_PERCEPTION_INFERENCE_YOLOV8_OIV7_BACKEND_H_
#define TRAFFIC_PERCEPTION_INFERENCE_YOLOV8_OIV7_BACKEND_H_

#include <opencv2/dnn.hpp>
#include <string>
#include <vector>
#include "traffic_perception/inference/imodel_backend.h"

namespace traffic_perception {

class YoloV8OIV7Backend : public IModelBackend {
 public:
  explicit YoloV8OIV7Backend(const std::string& modelPath);
  ~YoloV8OIV7Backend() override = default;

  InferenceResult infer(const Frame& frame) override;
  std::string getModelName() const override;
  void draw(cv::Mat& image,
            const InferenceResult& inference) override;

 private:
  cv::dnn::Net net_;
  float confThreshold_ = 0.5f;
  float nmsThreshold_ = 0.4f;

  void preprocess(const cv::Mat& frame, cv::Mat& blob);
  void postprocess(const cv::Mat& frame, const std::vector<cv::Mat>& outs, InferenceResult& result);
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_INFERENCE_YOLOV8_OIV7_BACKEND_H_
