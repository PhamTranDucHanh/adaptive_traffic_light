#include "timing_decision_application.h"

#include <sched.h>
#include <sys/mman.h>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <score/stop_token.hpp>
#include <string_view>

#ifdef RT_THREAD_CHECKING
#include <linux/sched.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "application_logger.h"
#include "common.h"
#include "score/mw/log/rust/stdout_logger_init.h"

namespace {

constexpr std::string_view kDefaultTimingReportDirectory{
    "/tmp/linux_rt_application/logs"};
constexpr char kTimingReportDirectoryEnvironment[] = "TIMING_REPORT_LOG_DIR";
constexpr std::int32_t kTimingDecisionCpu{1};

enum class RealtimeMemoryConfiguration : std::uint32_t {
  // A bounded reserve for the periodic thread's nested call stack.
  kStackPrefaultBytes = 65536U,
};

enum class TimeConversion : std::uint64_t {
  kNanosecondsPerMillisecond = 1000000ULL,
  kNanosecondsPerSecond = 1000000000ULL,
};

constexpr std::uint64_t toNanoseconds(const TimeConversion value) noexcept {
  return static_cast<std::uint64_t>(value);
}

constexpr std::uint32_t toBytes(
    const RealtimeMemoryConfiguration value) noexcept {
  return static_cast<std::uint32_t>(value);
}


void prefaultCurrentThreadStack() noexcept {
  std::array<std::uint8_t,
             toBytes(RealtimeMemoryConfiguration::kStackPrefaultBytes)>
      stackReserve;

  // Volatile writes force physical backing and resolve copy-on-write faults
  // before the periodic time-critical section begins.
  for (volatile std::uint8_t& stackByte : stackReserve) {
    stackByte = std::uint8_t{};
  }
}

std::uint64_t timestampNanoseconds(const timespec& timestamp) noexcept {
  return static_cast<std::int64_t>(timestamp.tv_sec) *
             static_cast<std::int64_t>(
                 toNanoseconds(TimeConversion::kNanosecondsPerSecond)) +
         static_cast<std::int64_t>(timestamp.tv_nsec);
}


std::uint64_t durationNanoseconds(const timespec& start,
                                  const timespec& end) noexcept {
  const std::int64_t duration = timestampNanoseconds(end) - timestampNanoseconds(start);
  return duration;
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

TimingDecisionApplication::~TimingDecisionApplication() {
#ifdef RT_THREAD_CHECKING
  stopRtChildThread();
#endif
  unlockProcessMemory();
}

#ifdef RT_THREAD_CHECKING
void* TimingDecisionApplication::childThreadEntry(void* const application) {
  TimingDecisionApplication* const self =
      static_cast<TimingDecisionApplication*>(application);
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

bool TimingDecisionApplication::lockProcessMemory() noexcept {
  if (memoryLocked_) {
    return true;
  }

  if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
    const std::int32_t lockError = errno;
    applicationLogger().LogError()
        << "[INIT][MEMORY_LOCK] mlockall(MCL_CURRENT|MCL_FUTURE) failed; "
           "errno="
        << lockError
        << "; reason=" << std::string_view{std::strerror(lockError)}
        << "; raise RLIMIT_MEMLOCK or grant CAP_IPC_LOCK";
    return false;
  }

  memoryLocked_ = true;
  applicationLogger().LogInfo() << "[INIT][MEMORY_LOCK] process memory locked; "
                                   "flags=MCL_CURRENT|MCL_FUTURE";
  return true;
}

void TimingDecisionApplication::unlockProcessMemory() noexcept {
  if (!memoryLocked_) {
    return;
  }

  if (munlockall() != 0) {
    const std::int32_t unlockError = errno;
    applicationLogger().LogError()
        << "[STOP][MEMORY_LOCK] munlockall failed; errno=" << unlockError
        << "; reason=" << std::string_view{std::strerror(unlockError)};
    return;
  }
  memoryLocked_ = false;
}

std::int32_t TimingDecisionApplication::Initialize(
    const score::mw::lifecycle::ApplicationContext& context) {
  (void)context;

  // S-CORE HealthMonitor uses score_log's Rust frontend internally. Install
  // the same process-local bridge used by the official Signal Controller
  // before constructing the monitor; application DLT records continue to use
  // score::mw::log and timing_decision_logging.json.
  score::mw::log::rust::StdoutLoggerBuilder loggerBuilder;
  loggerBuilder.Context("TDEC")
      .LogLevel(score::mw::log::rust::LogLevel::Verbose)
      .SetAsDefaultLogger();

  cpu_set_t affinityMask;
  CPU_ZERO(&affinityMask);
  CPU_SET(kTimingDecisionCpu, &affinityMask);
  if (sched_setaffinity(0, sizeof(affinityMask), &affinityMask) != 0) {
    const std::int32_t affinityError = errno;
    applicationLogger().LogError()
        << "[INIT][CPU_AFFINITY] sched_setaffinity failed; cpu="
        << kTimingDecisionCpu << "; errno=" << affinityError
        << "; reason=" << std::string_view{std::strerror(affinityError)};
    return EXIT_FAILURE;
  }
  applicationLogger().LogInfo()
      << "[INIT][CPU_AFFINITY] process pinned; cpu=" << kTimingDecisionCpu;

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

  if (!lockProcessMemory()) {
#ifdef RT_THREAD_CHECKING
    stopRtChildThread();
#endif
    service_.shutdown();
    timingReportLogger_.shutdown();
    initialized_ = false;
    return EXIT_FAILURE;
  }

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

  prefaultCurrentThreadStack();
  applicationLogger().LogInfo()
      << "[RUN][MEMORY_LOCK] periodic thread stack prefaulted; reserve_bytes="
      << toBytes(RealtimeMemoryConfiguration::kStackPrefaultBytes);

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
    unlockProcessMemory();
    initialized_ = false;
    return EXIT_FAILURE;
  }
  common::addMilliseconds(nextRelease, service_.periodMs());

  score::cpp::stop_callback stopRequestCallback{
      stopToken, [this]() noexcept { periodicWait_.requestStop(); }};
  applicationLogger().LogInfo()
      << "[RUN] periodic loop started; clock=CLOCK_MONOTONIC; "
         "wait=clock_nanosleep; deadline=absolute; stop_wait_bound_ms="
      << service_.periodMs();

  std::uint64_t cycleId = 0;
  std::int32_t sleepResult = 0;
  bool wakeupTimestampValid = false;
  timespec actualWakeup{};
  timespec executionStart{};
  timespec executionEnd{};
  bool executionStartValid = false;
  bool cycleSucceeded = false;
  bool executionEndTimestampValid = false;
  std::uint64_t deadlineNs = 0;
  WakeupTimingRecord wakeupRecord = {};
  std::uint64_t executionTimeNs = 0;
  std::uint64_t responseTimeNs = 0;
  std::uint64_t executionOverrunNs = 0;
  std::uint64_t cycleOverrunNs = 0;
  bool executionDeadlineMiss = false;
  bool cycleDeadlineMiss = false;
  ExecutionTimingRecord executionRecord = {};
  timespec now{};
  std::uint32_t skipped = 0;

  while (!stopToken.stop_requested()) {
    cycleId = cycleCount_ + std::uint64_t{1U};
    sleepResult = periodicWait_.waitUntil(nextRelease);
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
    wakeupTimestampValid = common::monotonicNow(actualWakeup);

    // These two timestamps intentionally bracket only runDecisionCycle().
    executionStartValid = common::monotonicNow(executionStart);
    cycleSucceeded = service_.runDecisionCycle();
    executionEndTimestampValid = common::monotonicNow(executionEnd);

    deadlineNs = periodNanoseconds(service_.periodMs());
    if (wakeupTimestampValid) {
      wakeupRecord = WakeupTimingRecord {
        cycleId, timestampNanoseconds(nextRelease),
        timestampNanoseconds(actualWakeup),
        durationNanoseconds(nextRelease, actualWakeup), deadlineNs};
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
      executionTimeNs = durationNanoseconds(executionStart, executionEnd);
      responseTimeNs = durationNanoseconds(nextRelease, executionEnd);
      executionOverrunNs = deadlineOverrunNanoseconds(executionTimeNs, deadlineNs);
      cycleOverrunNs = deadlineOverrunNanoseconds(responseTimeNs, deadlineNs);
      executionDeadlineMiss = { executionOverrunNs != 0 };
      cycleDeadlineMiss = { cycleOverrunNs != 0 };
      if (cycleDeadlineMiss) {
        ++deadlineMissCount_;
      }

      executionRecord = ExecutionTimingRecord {
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
    if (common::monotonicNow(now)) {
      skipped = common::advancePastNow(nextRelease, service_.periodMs(), now);
      if (skipped != 0) {
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
  unlockProcessMemory();
  return exitCode;
}

}  // namespace traffic_timing_decision
