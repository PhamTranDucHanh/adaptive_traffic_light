#ifndef INFERENCE_MODEL_BACKEND_H
#define INFERENCE_MODEL_BACKEND_H

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

#include "traffic_perception/core/types.h"

class ModelBackend {
 private:
  void *InternalModelPtr;

 public:
  bool init(const std::string &modelPath);
  static std::vector<Detection> detect(const cv::Mat &frame);
};

#endif  // INFERENCE_MODEL_BACKEND_H
