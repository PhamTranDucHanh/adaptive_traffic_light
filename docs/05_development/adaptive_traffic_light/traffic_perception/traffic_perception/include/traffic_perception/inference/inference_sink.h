#ifndef TRAFFIC_PERCEPTION_INFERENCE_INFERENCE_SINK_H_
#define TRAFFIC_PERCEPTION_INFERENCE_INFERENCE_SINK_H_

#include "traffic_perception/core/types.h"
#include "traffic_perception/inference/inference_result.h"

namespace traffic_perception {

// Interface for the next pipeline stage (e.g., Analyzer).
// InferenceEngine transfers Frame ownership to the sink.
class IInferenceSink {
 public:
  virtual ~IInferenceSink() = default;
  
  // Accepts a frame and its corresponding inference result.
  // Ownership of frame is transferred to the sink.
  //
  // After this function returns successfully,
  // the caller must no longer access or release
  // the Frame.
  virtual void accept(Frame* frame, InferenceResult&& result) = 0;
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_INFERENCE_INFERENCE_SINK_H_
