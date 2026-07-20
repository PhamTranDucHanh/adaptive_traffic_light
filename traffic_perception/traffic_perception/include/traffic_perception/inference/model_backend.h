#ifndef INFERENCE_MODEL_BACKEND_H
#define INFERENCE_MODEL_BACKEND_H

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

#include "traffic_perception/core/types.h"

namespace traffic_perception {

class ModelBackend {
 private:
  void *InternalModelPtr;

 public:
  bool init(const std::string &modelPath);
  static std::vector<Detection> detect(const cv::Mat &frame);
};

}  // namespace traffic_perception

#endif  // INFERENCE_MODEL_BACKEND_H
