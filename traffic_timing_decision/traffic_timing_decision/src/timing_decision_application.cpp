#include "timing_decision_application.h"

#include <cstdlib>
#include <cstring>
#include <score/stop_token.hpp>
#include <string_view>

#ifdef RT_THREAD_CHECKING
#include <linux/sched.h>
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#endif

#include "application_logger.h"
#include "common.h"

#ifdef RT_THREAD_CHECKING
namespace {

constexpr char kRtChildThreadName[] = "timing_rt_child";
constexpr useconds_t kChildStopPollingIntervalUs = 100000U;

// Create a pthread with an explicit real-time policy instead of inheriting
// scheduling attributes implicitly from the caller.
int create_rt_thread(pthread_t* const thread, void* (*const entryPoint)(void*),
                     void* const argument, const int priority) noexcept {
  pthread_attr_t attributes{};
  int result = pthread_attr_init(&attributes);
  if (result != 0) {
    return result;
  }

  result = pthread_attr_setinheritsched(&attributes, PTHREAD_EXPLICIT_SCHED);
  if (result == 0) {
    result = pthread_attr_setschedpolicy(&attributes, SCHED_FIFO);
  }
  if (result == 0) {
    sched_param parameters{};
    parameters.sched_priority = priority;
    result = pthread_attr_setschedparam(&attributes, &parameters);
  }
  if (result == 0) {
    result = pthread_create(thread, &attributes, entryPoint, argument);
  }

  // Once pthread_create() succeeds, attribute cleanup must not make the caller
  // treat the already-running thread as if creation had failed.
  (void)pthread_attr_destroy(&attributes);
  return result;
}

}  // namespace
#endif

namespace traffic_timing_decision {

#ifdef RT_THREAD_CHECKING
TimingDecisionApplication::~TimingDecisionApplication() { stopRtChildThread(); }

void* TimingDecisionApplication::childThreadEntry(void* const application) {
  auto* const self = static_cast<TimingDecisionApplication*>(application);
  self->runRtChildThread();
  return nullptr;
}

bool TimingDecisionApplication::startRtChildThread() noexcept {
  if (childThreadCreated_) {
    return true;
  }

  int parentPolicy{0};
  sched_param parentParameters{};
  const int queryResult =
      pthread_getschedparam(pthread_self(), &parentPolicy, &parentParameters);
  if (queryResult != 0) {
    applicationLogger().LogError()
        << "[RT_THREAD_CHECK][CREATE] could not read parent scheduling: "
        << std::string_view{std::strerror(queryResult)};
    return false;
  }

  if (parentPolicy != SCHED_FIFO) {
    applicationLogger().LogError()
        << "[RT_THREAD_CHECK][CREATE] parent policy is not SCHED_FIFO; policy="
        << parentPolicy;
    return false;
  }

  const int minimumPriority = sched_get_priority_min(SCHED_FIFO);
  const int childPriority = parentParameters.sched_priority > minimumPriority
                                ? parentParameters.sched_priority - 1
                                : minimumPriority;

  childThreadRunning_.store(true, std::memory_order_release);
  const int createResult = create_rt_thread(
      &childThread_, &TimingDecisionApplication::childThreadEntry, this,
      childPriority);
  if (createResult != 0) {
    childThreadRunning_.store(false, std::memory_order_release);
    applicationLogger().LogError()
        << "[RT_THREAD_CHECK][CREATE] pthread creation failed: "
        << std::string_view{std::strerror(createResult)}
        << "; requested_policy=SCHED_FIFO; requested_priority="
        << childPriority;
    return false;
  }

  childThreadCreated_ = true;
  applicationLogger().LogInfo()
      << "[RT_THREAD_CHECK][CREATE] child created; parent_priority="
      << parentParameters.sched_priority
      << "; child_priority=" << childPriority;
  return true;
}

void TimingDecisionApplication::stopRtChildThread() noexcept {
  if (!childThreadCreated_) {
    return;
  }

  childThreadRunning_.store(false, std::memory_order_release);
  const int joinResult = pthread_join(childThread_, nullptr);
  if (joinResult != 0) {
    applicationLogger().LogError()
        << "[RT_THREAD_CHECK][STOP] pthread_join failed: "
        << std::string_view{std::strerror(joinResult)};
  } else {
    applicationLogger().LogInfo() << "[RT_THREAD_CHECK][STOP] child joined";
  }
  childThreadCreated_ = false;
}

void TimingDecisionApplication::runRtChildThread() noexcept {
  const int nameResult = pthread_setname_np(pthread_self(), kRtChildThreadName);

  int policy{0};
  sched_param parameters{};
  const int schedulingResult =
      pthread_getschedparam(pthread_self(), &policy, &parameters);
  const auto threadId = static_cast<long>(syscall(SYS_gettid));

  if (nameResult != 0 || schedulingResult != 0) {
    applicationLogger().LogError()
        << "[RT_THREAD_CHECK][CHILD] setup failed; tid=" << threadId
        << "; name_result=" << nameResult
        << "; scheduling_result=" << schedulingResult;
  } else {
    applicationLogger().LogInfo()
        << "[RT_THREAD_CHECK][CHILD] hello from child thread; tid=" << threadId
        << "; name=" << kRtChildThreadName << "; policy=" << policy
        << "; priority=" << parameters.sched_priority;
  }

  while (childThreadRunning_.load(std::memory_order_acquire)) {
    if (usleep(kChildStopPollingIntervalUs) != 0 && errno != EINTR) {
      applicationLogger().LogError()
          << "[RT_THREAD_CHECK][CHILD] usleep failed; errno=" << errno;
      break;
    }
  }
}
#endif

std::int32_t TimingDecisionApplication::Initialize(
    const score::mw::lifecycle::ApplicationContext& context) {
  (void)context;

  if (!periodicWait_.valid()) {
    applicationLogger().LogError()
        << "[INIT][PERIODIC] condition variable initialization failed";
    return EXIT_FAILURE;
  }

  initialized_ = service_.initialize();
  if (!initialized_) {
    applicationLogger().LogError() << "[INIT] service initialization failed";
    return EXIT_FAILURE;
  }

#ifdef RT_THREAD_CHECKING
  if (!startRtChildThread()) {
    service_.shutdown();
    initialized_ = false;
    return EXIT_FAILURE;
  }
#endif

  cycleCount_ = 0U;
  applicationLogger().LogInfo()
      << "[INIT] service ready; period_ms=" << service_.periodMs();
  return EXIT_SUCCESS;
}

std::int32_t TimingDecisionApplication::Run(
    const score::cpp::stop_token& stopToken) {
  if (!initialized_) {
    return EXIT_FAILURE;
  }

  std::int32_t exitCode{EXIT_SUCCESS};
  timespec nextRelease{};
  if (!common::monotonicNow(nextRelease)) {
    applicationLogger().LogError()
        << "[RUN] clock_gettime(CLOCK_MONOTONIC) failed";
#ifdef RT_THREAD_CHECKING
    stopRtChildThread();
#endif
    service_.shutdown();
    initialized_ = false;
    return EXIT_FAILURE;
  }
  common::addMilliseconds(nextRelease, service_.periodMs());

  score::cpp::stop_callback stopWake{
      stopToken, [this]() noexcept { periodicWait_.requestStop(); }};
  applicationLogger().LogInfo()
      << "[RUN] periodic loop started; clock=CLOCK_MONOTONIC; "
         "wait=pthread_cond_timedwait; deadline=absolute";
  while (!stopToken.stop_requested()) {
    const int sleepResult = periodicWait_.waitUntil(nextRelease);
    if (sleepResult == ECANCELED || stopToken.stop_requested()) {
      break;
    }
    if (sleepResult != 0) {
      applicationLogger().LogError()
          << "[RUN] periodic wait failed: "
          << std::string_view{std::strerror(sleepResult)};
      exitCode = EXIT_FAILURE;
      break;
    }

    if (!service_.runDecisionCycle()) {
      applicationLogger().LogError()
          << "[RUN] health-monitored decision cycle failed";
      exitCode = EXIT_FAILURE;
      break;
    }
    ++cycleCount_;
#ifdef LOG_NUMBER_CYCLES
    applicationLogger().LogDebug()
        << "[CYCLE] completed; counter=" << cycleCount_
        << "; period_ms=" << service_.periodMs();
#endif

    common::addMilliseconds(nextRelease, service_.periodMs());
    timespec now{};
    if (common::monotonicNow(now)) {
      const std::uint32_t skipped =
          common::advancePastNow(nextRelease, service_.periodMs(), now);
      if (skipped > 0U) {
        applicationLogger().LogWarn()
            << "[RUN][OVERRUN] skipped_releases=" << skipped;
      }
    }
  }

#ifdef RT_THREAD_CHECKING
  stopRtChildThread();
#endif
  service_.shutdown();
  initialized_ = false;
  applicationLogger().LogInfo() << "[STOP] cycles_completed=" << cycleCount_;
  return exitCode;
}

}  // namespace traffic_timing_decision
