#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "traffic_perception/perception_module.h"

int main() {
  using namespace traffic_perception;
  PerceptionModule module{};
  if (!module.initModule("") || !module.startThreads()) {
    std::cerr << "[PERCEPTION_SMOKE][FAIL] initialization\n";
    module.stopThreads();
    return EXIT_FAILURE;
  }

  constexpr std::uint32_t kCycles{5U};
  constexpr auto kPeriod = std::chrono::milliseconds{1000};
  auto nextRelease =
      std::chrono::steady_clock::now() + kPeriod;

  for (std::uint32_t cycle = 1U; cycle <= kCycles; ++cycle) {
    std::this_thread::sleep_until(nextRelease);

    if (!module.processAndPublishOneSnapshot()) {
      std::cerr << "[PERCEPTION_SMOKE][FAIL] cycle="
                << cycle << '\n';
      module.stopThreads();
      return EXIT_FAILURE;
    }

    std::cout << "[PERCEPTION_SMOKE] cycle=" << cycle
              << "; poc_sender_ack=true\n";
    nextRelease += kPeriod;
  }

  module.stopThreads();
  std::cout << "[PERCEPTION_SMOKE][PASS] cycles="
            << kCycles << '\n';
  return EXIT_SUCCESS;
}
