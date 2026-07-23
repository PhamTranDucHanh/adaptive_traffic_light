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

namespace {

constexpr std::string_view kDefaultTimingReportDirectory{
    "/tmp/linux_rt_application/logs"};
constexpr char kTimingReportDirectoryEnvironment[] = "TIMING_REPORT_LOG_DIR";

enum class TimeConversion : std::uint64_t {
  kNanosecondsPerMillisecond = 1000000ULL,
  kNanosecondsPerSecond = 1000000000ULL,
};

constexpr std::uint64_t toNanoseconds(const TimeConversion value) noexcept {
  return static_cast<std::uint64_t>(value);
}

std::int64_t timestampNanoseconds(const timespec& timestamp) noexcept {
  return static_cast<std::int64_t>(timestamp.tv_sec) *
             static_cast<std::int64_t>(
                 toNanoseconds(TimeConversion::kNanosecondsPerSecond)) +
         static_cast<std::int64_t>(timestamp.tv_nsec);
}

std::int64_t signedDurationNanoseconds(const timespec& start,
                                       const timespec& end) noexcept {
  return timestampNanoseconds(end) - timestampNanoseconds(start);
}

std::uint64_t durationNanoseconds(const timespec& start,
                                  const timespec& end) noexcept {
  const std::int64_t duration = signedDurationNanoseconds(start, end);
  return duration > std::int64_t{} ? static_cast<std::uint64_t>(duration)
                                   : std::uint64_t{};
}

std::uint64_t periodNanoseconds(
    const std::uint32_t periodMilliseconds) noexcept {
  return static_cast<std::uint64_t>(periodMilliseconds) *
         toNanoseconds(TimeConversion::kNanosecondsPerMillisecond);
}

std::uint64_t deadlineOverrunNanoseconds(
    const std::uint64_t elapsedNanoseconds,
    const std::uint64_t deadlineNanoseconds) noexcept {
  return elapsedNanoseconds > deadlineNanoseconds
             ? elapsedNanoseconds - deadlineNanoseconds
             : std::uint64_t{};
}

std::string_view timingReportDirectory() noexcept {
  const char* const configuredDirectory =
      std::getenv(kTimingReportDirectoryEnvironment);
  if (configuredDirectory == nullptr || configuredDirectory[0] == '\0') {
    return kDefaultTimingReportDirectory;
  }
  return std::string_view{configuredDirectory};
}

}  // namespace

#ifdef RT_THREAD_CHECKING
namespace {

constexpr char kRtChildThreadName[] = "timing_rt_child";

enum class RtThreadConfiguration : std::int32_t {
  kChildPriorityOffset = 1,
};

enum class RtThreadIntervalMicroseconds : std::uint32_t {
  kChildStopPolling = 100000U,
};

constexpr std::int32_t toValue(const RtThreadConfiguration value) noexcept {
  return static_cast<std::int32_t>(value);
}

constexpr std::uint32_t toMicroseconds(
    const RtThreadIntervalMicroseconds value) noexcept {
  return static_cast<std::uint32_t>(value);
}

// Create a pthread with an explicit real-time policy instead of inheriting
// scheduling attributes implicitly from the caller.
std::int32_t create_rt_thread(pthread_t* const thread,
                              void* (*const entryPoint)(void*),
                              void* const argument,
                              const std::int32_t priority) noexcept {
  pthread_attr_t attributes{};
  std::int32_t result = pthread_attr_init(&attributes);
  if (result != std::int32_t{}) {
    return result;
  }

  result = pthread_attr_setinheritsched(&attributes, PTHREAD_EXPLICIT_SCHED);
  if (result == std::int32_t{}) {
    result = pthread_attr_setschedpolicy(&attributes, SCHED_FIFO);
  }
  if (result == std::int32_t{}) {
    sched_param parameters{};
    parameters.sched_priority = priority;
    result = pthread_attr_setschedparam(&attributes, &parameters);
  }
  if (result == std::int32_t{}) {
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

  std::int32_t parentPolicy{};
  sched_param parentParameters{};
  const std::int32_t queryResult =
      pthread_getschedparam(pthread_self(), &parentPolicy, &parentParameters);
  if (queryResult != std::int32_t{}) {
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

  const std::int32_t minimumPriority = sched_get_priority_min(SCHED_FIFO);
  const std::int32_t childPriority =
      parentParameters.sched_priority > minimumPriority
          ? parentParameters.sched_priority -
                toValue(RtThreadConfiguration::kChildPriorityOffset)
          : minimumPriority;

  childThreadRunning_.store(true, std::memory_order_release);
  const std::int32_t createResult = create_rt_thread(
      &childThread_, &TimingDecisionApplication::childThreadEntry, this,
      childPriority);
  if (createResult != std::int32_t{}) {
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
  const std::int32_t joinResult = pthread_join(childThread_, nullptr);
  if (joinResult != std::int32_t{}) {
    applicationLogger().LogError()
        << "[RT_THREAD_CHECK][STOP] pthread_join failed: "
        << std::string_view{std::strerror(joinResult)};
  } else {
    applicationLogger().LogInfo() << "[RT_THREAD_CHECK][STOP] child joined";
  }
  childThreadCreated_ = false;
}

void TimingDecisionApplication::runRtChildThread() noexcept {
  const std::int32_t nameResult =
      pthread_setname_np(pthread_self(), kRtChildThreadName);

  std::int32_t policy{};
  sched_param parameters{};
  const std::int32_t schedulingResult =
      pthread_getschedparam(pthread_self(), &policy, &parameters);
  const std::int64_t threadId = static_cast<std::int64_t>(syscall(SYS_gettid));

  if (nameResult != std::int32_t{} || schedulingResult != std::int32_t{}) {
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
    if (usleep(
            toMicroseconds(RtThreadIntervalMicroseconds::kChildStopPolling)) !=
            std::int32_t{} &&
        errno != EINTR) {
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

  if (!timingReportLogger_.initialize(timingReportDirectory())) {
    applicationLogger().LogError()
        << "[INIT][TIMING_REPORT] could not create WKUP/EXEC DLT recorders";
    return EXIT_FAILURE;
  }

  initialized_ = service_.initialize();
  if (!initialized_) {
    applicationLogger().LogError() << "[INIT] service initialization failed";
    timingReportLogger_.shutdown();
    return EXIT_FAILURE;
  }

#ifdef RT_THREAD_CHECKING
  if (!startRtChildThread()) {
    service_.shutdown();
    timingReportLogger_.shutdown();
    initialized_ = false;
    return EXIT_FAILURE;
  }
#endif

  cycleCount_ = std::uint64_t{};
  deadlineMissCount_ = std::uint64_t{};
  applicationLogger().LogInfo()
      << "[INIT] service ready; period_ms=" << service_.periodMs()
      << "; timing_report_dir=" << timingReportDirectory();
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
    timingReportLogger_.shutdown();
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
    const std::uint64_t cycleId = cycleCount_ + std::uint64_t{1U};
    const std::int32_t sleepResult = periodicWait_.waitUntil(nextRelease);
    if (sleepResult == ECANCELED || stopToken.stop_requested()) {
      break;
    }
    if (sleepResult != std::int32_t{}) {
      applicationLogger().LogError()
          << "[RUN] periodic wait failed: "
          << std::string_view{std::strerror(sleepResult)};
      exitCode = EXIT_FAILURE;
      break;
    }

    // Capture the actual wake-up before doing any logging or domain work.
    timespec actualWakeup{};
    const bool wakeupTimestampValid = common::monotonicNow(actualWakeup);

    // These two timestamps intentionally bracket only runDecisionCycle().
    timespec executionStart{};
    const bool executionStartValid = common::monotonicNow(executionStart);
    const bool cycleSucceeded = service_.runDecisionCycle();
    timespec executionEnd{};
    const bool executionEndTimestampValid = common::monotonicNow(executionEnd);

    const std::uint64_t deadlineNs = periodNanoseconds(service_.periodMs());
    if (wakeupTimestampValid) {
      const WakeupTimingRecord wakeupRecord{
          cycleId, timestampNanoseconds(nextRelease),
          timestampNanoseconds(actualWakeup),
          signedDurationNanoseconds(nextRelease, actualWakeup), deadlineNs};
      if (!timingReportLogger_.logWakeup(wakeupRecord)) {
        applicationLogger().LogWarn()
            << "[TIMING_REPORT][WAKEUP] record dropped; cycle=" << cycleId;
      }
    } else {
      applicationLogger().LogError()
          << "[TIMING_REPORT][WAKEUP] clock_gettime(CLOCK_MONOTONIC) failed; "
             "cycle="
          << cycleId;
    }

    if (executionStartValid && executionEndTimestampValid) {
      const std::uint64_t executionTimeNs =
          durationNanoseconds(executionStart, executionEnd);
      const std::uint64_t responseTimeNs =
          durationNanoseconds(nextRelease, executionEnd);
      const std::uint64_t executionOverrunNs =
          deadlineOverrunNanoseconds(executionTimeNs, deadlineNs);
      const std::uint64_t cycleOverrunNs =
          deadlineOverrunNanoseconds(responseTimeNs, deadlineNs);
      const bool executionDeadlineMiss = executionOverrunNs != std::uint64_t{};
      const bool cycleDeadlineMiss = cycleOverrunNs != std::uint64_t{};
      if (cycleDeadlineMiss) {
        ++deadlineMissCount_;
      }

      const ExecutionTimingRecord executionRecord{
          cycleId,
          timestampNanoseconds(executionStart),
          timestampNanoseconds(executionEnd),
          executionTimeNs,
          responseTimeNs,
          deadlineNs,
          executionOverrunNs,
          cycleOverrunNs,
          executionDeadlineMiss,
          cycleDeadlineMiss,
          cycleSucceeded};
      if (!timingReportLogger_.logExecution(executionRecord)) {
        applicationLogger().LogWarn()
            << "[TIMING_REPORT][EXECUTION] record dropped; cycle=" << cycleId;
      }
    } else {
      applicationLogger().LogError()
          << "[TIMING_REPORT][EXECUTION] "
             "clock_gettime(CLOCK_MONOTONIC) failed; cycle="
          << cycleId;
    }

    if (!cycleSucceeded) {
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
      if (skipped != std::uint32_t{}) {
        applicationLogger().LogWarn()
            << "[RUN][OVERRUN] skipped_releases=" << skipped;
      }
    }
  }

#ifdef RT_THREAD_CHECKING
  stopRtChildThread();
#endif
  service_.shutdown();
  timingReportLogger_.shutdown();
  initialized_ = false;
  applicationLogger().LogInfo()
      << "[STOP] cycles_completed=" << cycleCount_
      << "; cycle_deadline_misses=" << deadlineMissCount_;
  return exitCode;
}

}  // namespace traffic_timing_decision
