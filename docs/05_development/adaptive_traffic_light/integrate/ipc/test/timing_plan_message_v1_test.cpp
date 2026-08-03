#include "traffic_ipc/timing_plan_message_v1.h"

#include <cstdlib>

int main() {
  traffic_ipc::TimingPlanMessageV1 message{};
  message.publisherInstanceId = 1U;
  message.sequenceNumber = 1U;

  if (!traffic_ipc::HasValidTimingPlanEnvelope(message)) {
    return EXIT_FAILURE;
  }

  message.emergencyNorthSouth = 2U;
  if (traffic_ipc::HasValidTimingPlanEnvelope(message)) {
    return EXIT_FAILURE;
  }

  message.emergencyNorthSouth = 0U;
  ++message.version;
  if (traffic_ipc::HasValidTimingPlanEnvelope(message)) {
    return EXIT_FAILURE;
  }

  message.version = traffic_ipc::kTimingPlanVersion;
  message.magic = 0U;
  if (traffic_ipc::HasValidTimingPlanEnvelope(message)) {
    return EXIT_FAILURE;
  }

  message.magic = traffic_ipc::kTimingPlanMagic;
  message.reserved[0U] = 1U;
  if (traffic_ipc::HasValidTimingPlanEnvelope(message)) {
    return EXIT_FAILURE;
  }

  message.reserved[0U] = 0U;
  message.messageSize = 0U;
  return traffic_ipc::HasValidTimingPlanEnvelope(message) ? EXIT_FAILURE
                                                          : EXIT_SUCCESS;
}
