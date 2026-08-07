#include "traffic_perception/pipeline/snapshot_publisher.h"

#include "score/mw/log/logger.h"

namespace {
inline score::mw::log::Logger& getPipeLogger() {
  static score::mw::log::Logger& logger =
      score::mw::log::CreateLogger("PLTM", "Traffic Perception Pipeline");
  return logger;
}
}  // namespace

namespace traffic_perception {

void SnapshotPublisher::initSender(ISnapshotSender* sender) {
  sender_ = sender;
}

bool SnapshotPublisher::broadcastSnapshot(const FrameContext& ctx) {
  Timeline tl = ctx.timeline;
  tl.add(TimelineStage::Publish);
  // Stream timeline fields manually to avoid missing operator<< overload
  getPipeLogger().LogInfo() << "FrameId=" << tl.frameId;
  for (const auto& entry : tl.entries) {
    int64_t absolute = entry.timestamp.time_since_epoch().count();
    getPipeLogger().LogInfo()
        << " " << static_cast<int>(entry.stage) << "=" << absolute;
  }
  getPipeLogger().LogInfo() << "\n";

  if (sender_ == nullptr) {
    getPipeLogger().LogDebug()
        << "[PERCEPTION][PUBLISH][ERROR] sender is null\n";
    return false;
  }
  // Forward the snapshot to the sender.
  return sender_->send(ctx.snapshot);
}

}  // namespace traffic_perception
