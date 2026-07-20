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
  virtual std::string getModelName() const = 0;
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_INFERENCE_IMODEL_BACKEND_H_
