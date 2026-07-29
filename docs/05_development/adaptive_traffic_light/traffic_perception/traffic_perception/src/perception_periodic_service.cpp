#include "traffic_perception/perception_periodic_service.h"

namespace traffic_perception {

bool PerceptionPeriodicService::initialize(
    const std::string& configPath) {
  if (running_) {
    return true;
  }

  if (!module_.initModule(configPath) || !module_.startThreads()) {
    module_.stopThreads();
    return false;
  }

  if (!health_.initialize()) {
    module_.stopThreads();
    return false;
  }

  running_ = true;
  return true;
}

bool PerceptionPeriodicService::runCycle() {
  if (!running_ || !health_.beginCycle()) {
    return false;
  }

  const bool published = module_.processAndPublishOneSnapshot();
  const bool healthFinished = health_.finishCycle(published);

  // PoC fail-fast policy: một publish failure làm process trả failure.
  // TODO(policy): chỉ thêm retry/transient policy sau khi team review rõ.
  return published && healthFinished;
}

void PerceptionPeriodicService::shutdown() {
  if (!running_) {
    module_.stopThreads();
    return;
  }

  health_.shutdown();
  module_.stopThreads();
  running_ = false;
}

std::chrono::milliseconds
PerceptionPeriodicService::period() const noexcept {
  return std::chrono::milliseconds{1000};
}

}  // namespace traffic_perception
