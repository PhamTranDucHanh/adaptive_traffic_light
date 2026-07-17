#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "traffic_signal_controller/controller_module.h"

int main() {
  traffic_signal_controller::ControllerModule module{};
  if (!module.initialize()) {
    std::cerr << "[CONTROLLER_SMOKE][FAIL] initialization\n";
    return EXIT_FAILURE;
  }

  constexpr std::uint32_t kCycles{10U};
  auto nextRelease =
      std::chrono::steady_clock::now() + module.period();

  for (std::uint32_t cycle = 1U; cycle <= kCycles; ++cycle) {
    std::this_thread::sleep_until(nextRelease);
    const auto result = module.runCycle();

    if (!result.success ||
        result.appliedPhase != PhaseId::ALL_RED) {
      std::cerr
          << "[CONTROLLER_SMOKE][FAIL] cycle=" << cycle
          << "; safe output not confirmed\n";
      (void)module.shutdown();
      return EXIT_FAILURE;
    }

    std::cout << "[CONTROLLER_SMOKE] cycle=" << cycle
              << "; applied=ALL_RED\n";
    nextRelease += module.period();
  }

  if (!module.shutdown()) {
    std::cerr << "[CONTROLLER_SMOKE][FAIL] shutdown ALL_RED\n";
    return EXIT_FAILURE;
  }

  std::cout << "[CONTROLLER_SMOKE][PASS] cycles="
            << kCycles << "; final_state=ALL_RED\n";
  return EXIT_SUCCESS;
}