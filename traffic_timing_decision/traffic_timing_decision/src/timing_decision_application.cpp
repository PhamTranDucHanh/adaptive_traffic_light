#include "timing_decision_application.h"

#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iostream>

#include <pthread.h>
#include <sched.h>

#include "score/concurrency/interruptible_wait.h"

namespace {

const char* schedulingPolicyName(const int policy) noexcept {
  switch (policy) {
    case SCHED_FIFO:
      return "SCHED_FIFO";
    case SCHED_RR:
      return "SCHED_RR";
    case SCHED_OTHER:
      return "SCHED_OTHER";
    default:
      return "UNKNOWN";
  }
}

bool verifyRealtimeScheduling() {
  int policy{SCHED_OTHER};
  sched_param parameters{};
  const int result =
      pthread_getschedparam(pthread_self(), &policy, &parameters);
  if (result != 0) {
    std::cerr << "[TIMING_DECISION][RT][ERROR] pthread_getschedparam: "
              << std::strerror(result) << '\n';
    return false;
  }

  const int minimumPriority = sched_get_priority_min(policy);
  const int maximumPriority = sched_get_priority_max(policy);
  std::cout << "[TIMING_DECISION][RT] policy="
            << schedulingPolicyName(policy)
            << " priority=" << parameters.sched_priority
            << " allowed_range=" << minimumPriority << ".."
            << maximumPriority << '\n';

  if (policy != SCHED_FIFO || parameters.sched_priority <= 0) {
    std::cerr << "[TIMING_DECISION][RT][ERROR] expected SCHED_FIFO with a "
                 "positive priority; check Lifecycle sandbox permissions\n";
    return false;
  }

  return true;
}

}  // namespace

namespace traffic_timing_decision {

std::int32_t TimingDecisionApplication::Initialize(
    const score::mw::lifecycle::ApplicationContext& context) {
  (void)context;
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  if (!verifyRealtimeScheduling()) {
    return EXIT_FAILURE;
  }

  initialized_ = service_.initialize();
  if (!initialized_) {
    std::cerr << "[TIMING_DECISION][INIT][ERROR] service initialization "
                 "failed\n";
    return EXIT_FAILURE;
  }

  cycleCount_ = 0U;
  std::cout << "[TIMING_DECISION][INIT] service ready; period_ms="
            << service_.periodMs() << '\n';
  return EXIT_SUCCESS;
}

std::int32_t TimingDecisionApplication::Run(
    const score::cpp::stop_token& stopToken) {
  if (!initialized_) {
    return EXIT_FAILURE;
  }

  const auto period = std::chrono::milliseconds{service_.periodMs()};
  auto nextRelease = std::chrono::steady_clock::now() + period;
  std::int32_t exitCode{EXIT_SUCCESS};

  std::cout << "[TIMING_DECISION][RUN] periodic loop started\n";
  while (!stopToken.stop_requested()) {
    // The first release is one full period after startup. This gives the
    // heartbeat monitor a real 2.5-second baseline; subsequent releases stay
    // anchored to this monotonic schedule and do not accumulate drift.
    if (score::concurrency::wait_until(stopToken, nextRelease)) {
      break;
    }

    if (!service_.runDecisionCycle()) {
      std::cerr << "[TIMING_DECISION][RUN][ERROR] health-monitored decision "
                   "cycle failed\n";
      exitCode = EXIT_FAILURE;
      break;
    }
    ++cycleCount_;
    std::cout << "[TIMING_DECISION][CYCLE] hello; counter=" << cycleCount_
              << "; period_ms=" << service_.periodMs() << '\n';

    nextRelease += period;
  }

  service_.shutdown();
  initialized_ = false;
  std::cout << "[TIMING_DECISION][STOP] cycles_completed=" << cycleCount_
            << '\n';
  return exitCode;
}

}  // namespace traffic_timing_decision
