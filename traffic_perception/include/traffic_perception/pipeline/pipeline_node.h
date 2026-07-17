#ifndef PIPELINE_PIPELINE_NODE_H
#define PIPELINE_PIPELINE_NODE_H

#include <variant>

#include "traffic_perception/pipeline/snapshot_publisher.h"
#include "traffic_perception/pipeline/traffic_analyzer.h"

namespace traffic_perception {

struct PipelineNode {
  std::variant<TrafficAnalyzer, SnapshotPublisher> Stage;
  PipelineNode* Next;
};

}  // namespace traffic_perception

#endif  // PIPELINE_PIPELINE_NODE_H
