#ifndef TRAFFIC_PERCEPTION_INFERENCE_IMODEL_BACKEND_H_
#define TRAFFIC_PERCEPTION_INFERENCE_IMODEL_BACKEND_H_

#include <memory>
#include <string>
#include "traffic_perception/inference/inference_result.h"
#include "traffic_perception/core/types.h"

namespace traffic_perception {

class IModelBackend {
 public:
  virtual ~IModelBackend() = default;
  virtual InferenceResult infer(const Frame& frame) = 0;
  void warmUp(const Resolution& resolution) {
    Frame frame{};
    frame.Image = cv::Mat::zeros(resolution.height, resolution.width, CV_8UC3);
    static_cast<void>(infer(frame));
  }
  virtual std::string getModelName() const = 0;
  virtual void draw(cv::Mat& image,
                    const InferenceResult& inference) = 0;
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_INFERENCE_IMODEL_BACKEND_H_
