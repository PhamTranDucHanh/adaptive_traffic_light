#ifndef TRAFFIC_PERCEPTION_PIPELINE_RENDER_PACKET_H_
#define TRAFFIC_PERCEPTION_PIPELINE_RENDER_PACKET_H_

#include "traffic_perception/core/types.h"
#include "traffic_perception/inference/inference_result.h"

namespace traffic_perception {

struct RenderPacket {
    Frame* frame{};
    InferenceResult inference;
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_PIPELINE_RENDER_PACKET_H_
