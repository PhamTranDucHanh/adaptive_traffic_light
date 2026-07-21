#include "traffic_signal_controller/signal_control_app.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <utility>

#include "score/concurrency/interruptible_wait.h"
#include "score/mw/log/rust/stdout_logger_init.h"

namespace traffic_signal_controller {

SignalControlApplication::~SignalControlApplication() {
  StopFsmWorker();
}

std::int32_t SignalControlApplication::Initialize(
    const score::mw::lifecycle::ApplicationContext& context) {
  (void)context;

  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  // HealthMonitor sử dụng Rust logger.
  score::mw::log::rust::StdoutLoggerBuilder loggerBuilder;
  loggerBuilder.Context("TSIG")
      .LogLevel(score::mw::log::rust::LogLevel::Verbose)
      .SetAsDefaultLogger();

  if (!healthReporter_.initialize()) {
    std::cerr
        << "[TRAFFIC_SIGNAL_CONTROLLER][INIT][ERROR] "
        << "HealthReporter initialization failed\n";

    return EXIT_FAILURE;
  }

  cycleCount_ = 0U;
  congestionPlanSent_ = false;
  emergencyPlanSent_ = false;

  fsmFailed_.store(false, std::memory_order_release);
  fsmRunning_.store(true, std::memory_order_release);

  try {
    fsmWorker_ = std::thread{
        &SignalControlApplication::RunFsmWorker,
        this};
  } catch (...) {
    fsmRunning_.store(false, std::memory_order_release);

    healthReporter_.shutdown();

    std::cerr
        << "[TRAFFIC_SIGNAL_CONTROLLER][INIT][ERROR] "
        << "Could not start FSM worker\n";

    return EXIT_FAILURE;
  }

  initialized_ = true;

  std::cout
      << "[TRAFFIC_SIGNAL_CONTROLLER][INIT] "
      << "application ready; period_ms=1000\n";

  return EXIT_SUCCESS;
}

std::int32_t SignalControlApplication::Run(
    const score::cpp::stop_token& stopToken) {
  if (!initialized_) {
    return EXIT_FAILURE;
  }

  using namespace std::chrono_literals;

  constexpr auto kPeriod = 1s;

  auto nextRelease =
      std::chrono::steady_clock::now() + kPeriod;

  std::int32_t exitCode{EXIT_SUCCESS};

  std::cout
      << "[TRAFFIC_SIGNAL_CONTROLLER][RUN] "
      << "periodic loop started\n";

  while (!stopToken.stop_requested()) {
    if (score::concurrency::wait_until(
            stopToken,
            nextRelease)) {
      break;
    }

    if (!healthReporter_.startControlCycle()) {
      std::cerr
          << "[TRAFFIC_SIGNAL_CONTROLLER][RUN][ERROR] "
          << "Could not start health-monitored control cycle\n";

      exitCode = EXIT_FAILURE;
      break;
    }

    /*
     * Basic demonstration:
     *
     * Cycle 5:
     *   Gửi normal congestion plan.
     *   Plan được áp dụng khi FSM đến ALL_RED.
     */
    if (!congestionPlanSent_ &&
        cycleCount_ >= 5U) {
      const TimingPlan congestionPlan =
          CreateCongestionPlan();

      congestionPlanSent_ =
          planReceiver_.ReceivePlan(congestionPlan);

      std::cout
          << "[TRAFFIC_SIGNAL_CONTROLLER][DEMO] "
          << "congestion_plan="
          << (congestionPlanSent_
                  ? "accepted"
                  : "rejected")
          << '\n';
    }

    /*
     * Cycle 15 trở đi:
     *   Thử gửi emergency plan.
     *
     * Emergency chỉ được channel nhận khi FSM đang GREEN.
     * Nếu FSM đang YELLOW hoặc ALL_RED, lifecycle sẽ thử lại
     * ở cycle tiếp theo.
     */
    if (congestionPlanSent_ &&
        !emergencyPlanSent_ &&
        cycleCount_ >= 15U) {
      const TimingPlan emergencyPlan =
          CreateEmergencyPlan();

      emergencyPlanSent_ =
          planReceiver_.ReceivePlan(emergencyPlan);

      if (emergencyPlanSent_) {
        std::cout
            << "[TRAFFIC_SIGNAL_CONTROLLER][DEMO] "
            << "emergency_plan=accepted\n";
      }
    }

    if (fsmFailed_.load(std::memory_order_acquire)) {
      std::cerr
          << "[TRAFFIC_SIGNAL_CONTROLLER][RUN][ERROR] "
          << "FSM worker failed\n";

      exitCode = EXIT_FAILURE;

      healthReporter_.finishControlCycle();
      break;
    }

    ++cycleCount_;

    healthReporter_.finishControlCycle();

    nextRelease += kPeriod;
  }

  StopFsmWorker();

  healthReporter_.shutdown();
  initialized_ = false;

  std::cout
      << "[TRAFFIC_SIGNAL_CONTROLLER][STOP] "
      << "cycles_completed="
      << cycleCount_
      << '\n';

  return exitCode;
}

void SignalControlApplication::RunFsmWorker() noexcept {
  try {
    while (fsmRunning_.load(
        std::memory_order_acquire)) {
      /*
       * processTick() tự thực hiện timing:
       *
       * GREEN:
       *   pthread_cond_timedwait()
       *
       * YELLOW / ALL_RED:
       *   clock_nanosleep()
       */
      const SignalDisplay display =
          signalFsmEngine_.processTick();

      /*
       * RequestShutdown() có thể đánh thức FSM.
       * Không publish thêm output sau khi shutdown.
       */
      if (!fsmRunning_.load(
              std::memory_order_acquire)) {
        break;
      }

      outputSimulator_.publish(display);
    }
  } catch (...) {
    fsmFailed_.store(
        true,
        std::memory_order_release);
  }
}

void SignalControlApplication::StopFsmWorker() noexcept {
  fsmRunning_.store(
      false,
      std::memory_order_release);

  /*
   * Đánh thức pthread_cond_timedwait() nếu FSM đang GREEN.
   */
  planSyncChannel_.RequestShutdown();

  if (fsmWorker_.joinable()) {
    fsmWorker_.join();
  }
}

TimingPlan
SignalControlApplication::CreateCongestionPlan() const {
  TimingPlan plan{};

  plan.planId = 1001U;

  // Mô phỏng hướng EW đông xe hơn.
  plan.greenNorthSouthMs = 15'000U;
  plan.greenEastWestMs = 30'000U;
  plan.yellowMs = 3'000U;
  plan.allRedMs = 1'000U;

  plan.cycleLengthMs =
      plan.greenNorthSouthMs +
      plan.greenEastWestMs +
      (2U * plan.yellowMs) +
      (2U * plan.allRedMs);

  plan.emergencyNorthSouth = false;
  plan.emergencyEastWest = false;

  return plan;
}

TimingPlan
SignalControlApplication::CreateEmergencyPlan() const {
  TimingPlan plan{};

  plan.planId = 2001U;

  // Mô phỏng xe ưu tiên theo hướng Bắc–Nam.
  plan.greenNorthSouthMs = 30'000U;
  plan.greenEastWestMs = 15'000U;
  plan.yellowMs = 3'000U;
  plan.allRedMs = 1'000U;

  plan.cycleLengthMs =
      plan.greenNorthSouthMs +
      plan.greenEastWestMs +
      (2U * plan.yellowMs) +
      (2U * plan.allRedMs);

  plan.emergencyNorthSouth = true;
  plan.emergencyEastWest = false;

  return plan;
}

}  // namespace traffic_signal_controller