#ifndef TRAFFIC_PERCEPTION_INFERENCE_DUMMY_MODEL_BACKEND_H_
#define TRAFFIC_PERCEPTION_INFERENCE_DUMMY_MODEL_BACKEND_H_

#include "traffic_perception/inference/imodel_backend.h"
#include <chrono>
#include <thread>

namespace traffic_perception {

class DummyModelBackend : public IModelBackend {
 public:
  InferenceResult infer(const Frame& frame) override {
    auto start = std::chrono::steady_clock::now();
    
    // Simulate inference latency
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    InferenceResult result;
    result.FrameId = frame.FrameId;
    
    // Create deterministic fake detections
    int numDetections = (frame.FrameId % 3) + 1;
    for (int i = 0; i < numDetections; ++i) {
        Detection d;
        d.ClassId = i;
        d.ClassName = "Car_" + std::to_string(i);
        d.Confidence = 0.9f;
        // Bounding boxes move frame by frame
        int x = 100 + (frame.FrameId * 5) % 400;
        int y = 120 + (i * 50);
        d.Box = cv::Rect(x, y, 50, 40);
        result.Detections.push_back(d);
    }
    
    auto end = std::chrono::steady_clock::now();
    result.InferenceLatencyMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    return result;
  }
  
  std::string getModelName() const override {
    return "Dummy Backend";
  }
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_INFERENCE_DUMMY_MODEL_BACKEND_H_
