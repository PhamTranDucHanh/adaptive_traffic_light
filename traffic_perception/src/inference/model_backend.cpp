#include "traffic_perception/inference/model_backend.h"

#include <iostream>

bool ModelBackend::init(const std::string &modelPath) {
  std::cout << "[ModelBackend] init() called with path: " << modelPath << '\n';
  InternalModelPtr = nullptr;
  return true;
}

std::vector<Detection> ModelBackend::detect(const cv::Mat &frame) {
  std::cout << "[ModelBackend] detect() called, frame size: " << frame.cols
            << "x" << frame.rows << '\n';
  std::vector<Detection> results;

  // Named constants to avoid magic numbers
  constexpr int32_t x_1 = 10;
  constexpr int32_t y_1 = 20;
  constexpr int32_t w_1 = 100;
  constexpr int32_t h_1 = 150;
  constexpr float conf_1 = 0.92F;

  Detection det1{cv::Rect(x_1, y_1, w_1, h_1), 2, conf_1};
  results.push_back(det1);

  return results;
}
