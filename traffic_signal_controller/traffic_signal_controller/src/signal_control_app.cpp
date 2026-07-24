#include "traffic_signal_controller/signal_control_app.h"

#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <array>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "analytics_service/analytics.h"
#include "common/logging_contexts.h"
#include "score/concurrency/interruptible_wait.h"
#include "score/mw/log/logger.h"
#include "score/mw/log/rust/stdout_logger_init.h"

namespace traffic_signal_controller {
namespace {

constexpr const char* kDefaultAnalyticsLogFile{"/tmp/CTRL.dlt"};
constexpr const char* kDefaultAnalyticsReportFile{
    "/tmp/traffic_signal_controller/logs/analytics_report.txt"};
constexpr const char* kRuntimeDirectory{"/tmp/traffic_signal_controller"};
constexpr const char* kRuntimeLogDirectory{
    "/tmp/traffic_signal_controller/logs"};

constexpr std::int32_t kDefaultFsmPriority{85};
constexpr std::int32_t kDecimalBase{10};

constexpr std::size_t kBytesPerKibibyte{1024U};
constexpr std::size_t kPrefaultStackSizeKibibytes{64U};
constexpr std::size_t kPrefaultStackBytes{
    kPrefaultStackSizeKibibytes * kBytesPerKibibyte};
constexpr std::size_t kPageSizeBytes{4096U};

constexpr mode_t kRuntimeDirectoryPermissions{0755};

constexpr std::uint64_t kCongestionPlanTriggerCycle{5U};
constexpr std::uint64_t kEmergencyPlanTriggerCycle{20U};
constexpr std::uint64_t kControlPeriodMilliseconds{1000U};

constexpr std::uint32_t kCongestionPlanId{1001U};
constexpr std::uint32_t kEmergencyPlanId{2001U};
constexpr std::uint32_t kShortGreenDurationMs{15'000U};
constexpr std::uint32_t kLongGreenDurationMs{30'000U};
constexpr std::uint32_t kYellowDurationMs{3'000U};
constexpr std::uint32_t kAllRedDurationMs{1'000U};
constexpr std::uint32_t kTransitionsPerCycle{2U};

struct SchedulingResult {
  bool success;
  std::int32_t errorNumber;
};

score::mw::log::Logger& AppLogger() {
  static auto& logger = score::mw::log::CreateLogger(ctrl::logging::kCtxApp,
                                                     "Signal Control App");
  return logger;
}

score::mw::log::Logger& AnalyticsLogger() {
  static auto& logger = score::mw::log::CreateLogger(
      ctrl::logging::kCtxAnalytics, "Analytics Shutdown Report");
  return logger;
}

const char* GetEnvOrDefault(const char* const name,
                            const char* const defaultValue) {
  const char* const value = std::getenv(name);
  return value != nullptr && value[0] != '\0' ? value : defaultValue;
}

std::int32_t GetPriorityFromEnvOrDefault(
    const char* const name, const std::int32_t defaultPriority) {
  const char* const value = std::getenv(name);

  if (value == nullptr || value[0] == '\0') {
    return defaultPriority;
  }

  std::int32_t parsedPriority{};
  const char* const valueEnd = value + std::strlen(value);
  const auto parseResult =
      std::from_chars(value, valueEnd, parsedPriority, kDecimalBase);

  if (parseResult.ec != std::errc{} || parseResult.ptr != valueEnd) {
    AppLogger().LogWarn() << "event=THREAD_PRIORITY_ENV_INVALID"
                          << ", variable=" << name << ", value=" << value
                          << ", fallback_priority=" << defaultPriority;
    return defaultPriority;
  }

  const std::int32_t minimumPriority = static_cast<std::int32_t>(
      sched_get_priority_min(SCHED_FIFO));
  const std::int32_t maximumPriority = static_cast<std::int32_t>(
      sched_get_priority_max(SCHED_FIFO));

  if (parsedPriority < minimumPriority || parsedPriority > maximumPriority) {
    AppLogger().LogWarn() << "event=THREAD_PRIORITY_ENV_INVALID"
                          << ", variable=" << name << ", value=" << value
                          << ", min_priority=" << minimumPriority
                          << ", max_priority=" << maximumPriority
                          << ", fallback_priority=" << defaultPriority;
    return defaultPriority;
  }

  return parsedPriority;
}

SchedulingResult SetFifoScheduling(const pthread_t thread,
                                   const std::int32_t priority) noexcept {
  sched_param parameters{};
  parameters.sched_priority = static_cast<int>(priority);

  const std::int32_t result = static_cast<std::int32_t>(
      pthread_setschedparam(thread, SCHED_FIFO, &parameters));
  return SchedulingResult{result == EXIT_SUCCESS, result};
}

void LogSchedulingResult(const char* const threadName,
                         const std::int32_t priority,
                         const SchedulingResult result) {
  if (result.success) {
    AppLogger().LogInfo() << "event=THREAD_SCHEDULING_CONFIGURED"
                          << ", thread=" << threadName
                          << ", policy=SCHED_FIFO"
                          << ", priority=" << priority;
    return;
  }

  AppLogger().LogWarn()
      << "event=THREAD_SCHEDULING_FAILED"
      << ", thread=" << threadName << ", policy=SCHED_FIFO"
      << ", priority=" << priority << ", error=" << result.errorNumber
      << ", reason=" << std::strerror(static_cast<int>(result.errorNumber));
}

void LockProcessMemory() {
  if (::mlockall(MCL_CURRENT | MCL_FUTURE) == EXIT_SUCCESS) {
    AppLogger().LogInfo() << "event=PROCESS_MEMORY_LOCKED"
                          << ", flags=MCL_CURRENT|MCL_FUTURE";
    return;
  }

  const std::int32_t errorNumber = static_cast<std::int32_t>(errno);
  AppLogger().LogWarn()
      << "event=PROCESS_MEMORY_LOCK_FAILED"
      << ", flags=MCL_CURRENT|MCL_FUTURE"
      << ", error=" << errorNumber
      << ", reason=" << std::strerror(static_cast<int>(errorNumber));
}

void PrefaultCurrentThreadStack(const char* const threadName) noexcept {
  std::array<std::uint8_t, kPrefaultStackBytes> stackPages{};
  volatile std::uint8_t* const writablePages = stackPages.data();

  for (std::size_t offset{0U}; offset < stackPages.size();
       offset += kPageSizeBytes) {
    writablePages[offset] = std::uint8_t{0U};
  }

  AppLogger().LogInfo() << "event=THREAD_STACK_PREFAULTED"
                        << ", thread=" << threadName
                        << ", bytes=" << stackPages.size();
}

void EnsureRuntimeLogDirectoryExists() {
  (void)mkdir(kRuntimeDirectory, kRuntimeDirectoryPermissions);
  (void)mkdir(kRuntimeLogDirectory, kRuntimeDirectoryPermissions);
}

std::uint32_t CalculateCycleLengthMs(const std::uint32_t northSouthGreenMs,
                                     const std::uint32_t eastWestGreenMs,
                                     const std::uint32_t yellowMs,
                                     const std::uint32_t allRedMs) noexcept {
  return northSouthGreenMs + eastWestGreenMs +
         (kTransitionsPerCycle * yellowMs) +
         (kTransitionsPerCycle * allRedMs);
}

}  // namespace

SignalControlApplication::~SignalControlApplication() {
  StopFsmWorker();
  outputSimulator_.stop();
}

std::int32_t SignalControlApplication::Initialize(
    const score::mw::lifecycle::ApplicationContext& context) {
  (void)context;

  score::mw::log::rust::StdoutLoggerBuilder loggerBuilder;
  loggerBuilder.Context("TSIG")
      .LogLevel(score::mw::log::rust::LogLevel::Verbose)
      .SetAsDefaultLogger();

  LockProcessMemory();
  PrefaultCurrentThreadStack("lifecycle_main");

  if (!healthReporter_.initialize()) {
    AppLogger().LogWarn() << "event=APP_INIT_FAILED"
                          << ", reason=HEALTH_REPORTER_INIT_FAILED";
    return EXIT_FAILURE;
  }

  // The Launch Manager owns the lifecycle/main-thread scheduling policy.
  // This application configures only its internal realtime worker threads.

  cycleCount_ = 0U;
  demoPlanReceiverState_ = DemoPlanReceiverState::kWaitingForCongestionPlan;
  fsmFailed_.store(false, std::memory_order_release);
  fsmRunning_.store(true, std::memory_order_release);

  try {
    outputSimulator_.start();
    fsmWorker_ = std::thread{&SignalControlApplication::RunFsmWorker, this};

    const std::int32_t fsmPriority = GetPriorityFromEnvOrDefault(
        "TRAFFIC_SIGNAL_CONTROLLER_FSM_PRIORITY", kDefaultFsmPriority);

    LogSchedulingResult(
        "fsm_worker", fsmPriority,
        SetFifoScheduling(fsmWorker_.native_handle(), fsmPriority));
  } catch (...) {
    fsmRunning_.store(false, std::memory_order_release);
    planSyncChannel_.RequestShutdown();

    if (fsmWorker_.joinable()) {
      fsmWorker_.join();
    }

    outputSimulator_.stop();
    healthReporter_.shutdown();

    AppLogger().LogWarn() << "event=APP_INIT_FAILED"
                          << ", reason=WORKER_START_FAILED";
    return EXIT_FAILURE;
  }

  initialized_ = true;
  AppLogger().LogInfo() << "event=APP_READY"
                        << ", period_ms=" << kControlPeriodMilliseconds;
  return EXIT_SUCCESS;
}

std::int32_t SignalControlApplication::Run(
    const score::cpp::stop_token& stopToken) {
  if (!initialized_) {
    return EXIT_FAILURE;
  }

  constexpr auto kControlPeriod =
      std::chrono::milliseconds{kControlPeriodMilliseconds};
  auto nextRelease = std::chrono::steady_clock::now() + kControlPeriod;
  std::int32_t exitCode{EXIT_SUCCESS};

  AppLogger().LogInfo() << "event=PERIODIC_LOOP_STARTED";

  while (!stopToken.stop_requested()) {
    if (score::concurrency::wait_until(stopToken, nextRelease)) {
      break;
    }

    if (!healthReporter_.startControlCycle()) {
      AppLogger().LogWarn() << "event=CONTROL_CYCLE_FAILED"
                            << ", reason=HEALTH_CYCLE_START_FAILED";
      exitCode = EXIT_FAILURE;
      break;
    }

    RunPlanReceiverSimulation();

    if (fsmFailed_.load(std::memory_order_acquire)) {
      AppLogger().LogWarn() << "event=CONTROL_CYCLE_FAILED"
                            << ", reason=FSM_WORKER_FAILED";
      exitCode = EXIT_FAILURE;
      healthReporter_.finishControlCycle();
      break;
    }

    ++cycleCount_;
    AppLogger().LogInfo() << "event=CONTROL_CYCLE_COMPLETED"
                          << ", cycle=" << cycleCount_
                          << ", period_ms=" << kControlPeriodMilliseconds;

    healthReporter_.finishControlCycle();
    nextRelease += kControlPeriod;
  }

  StopFsmWorker();
  outputSimulator_.stop();
  WriteAnalyticsReport();
  healthReporter_.shutdown();
  initialized_ = false;

  AppLogger().LogInfo() << "event=APP_STOPPED"
                        << ", cycles_completed=" << cycleCount_;
  return exitCode;
}

void SignalControlApplication::RunPlanReceiverSimulation() {
  switch (demoPlanReceiverState_) {
    case DemoPlanReceiverState::kWaitingForCongestionPlan: {
      if (cycleCount_ < kCongestionPlanTriggerCycle) {
        return;
      }

      const TimingPlan congestionPlan = CreateCongestionPlan();
      const bool accepted = planReceiver_.ReceivePlan(congestionPlan);

      AppLogger().LogInfo()
          << "event=SIMULATED_PLAN_SUBMITTED"
          << ", cycle=" << cycleCount_
          << ", source=simulated_plan_receiver"
          << ", plan_type=congestion"
          << ", result=" << (accepted ? "accepted" : "rejected");

      if (accepted) {
        demoPlanReceiverState_ =
            DemoPlanReceiverState::kWaitingForEmergencyPlan;
      }
      return;
    }

    case DemoPlanReceiverState::kWaitingForEmergencyPlan: {
      if (cycleCount_ < kEmergencyPlanTriggerCycle) {
        return;
      }

      const TimingPlan emergencyPlan = CreateEmergencyPlan();
      const bool accepted = planReceiver_.ReceivePlan(emergencyPlan);

      AppLogger().LogInfo()
          << "event=SIMULATED_PLAN_SUBMITTED"
          << ", cycle=" << cycleCount_
          << ", source=simulated_plan_receiver"
          << ", plan_type=emergency"
          << ", result=" << (accepted ? "accepted" : "retry");

      if (accepted) {
        demoPlanReceiverState_ = DemoPlanReceiverState::kCompleted;
      }
      return;
    }

    case DemoPlanReceiverState::kCompleted:
      return;
  }
}

void SignalControlApplication::WriteAnalyticsReport() const {
  EnsureRuntimeLogDirectoryExists();

  const std::string logFilePath = GetEnvOrDefault(
      "TRAFFIC_SIGNAL_CONTROLLER_ANALYTICS_LOG_FILE",
      kDefaultAnalyticsLogFile);
  const std::string reportFilePath = GetEnvOrDefault(
      "TRAFFIC_SIGNAL_CONTROLLER_ANALYTICS_REPORT_FILE",
      kDefaultAnalyticsReportFile);

  Analytics analytics{logFilePath};

  if (!analytics.Analyze()) {
    AnalyticsLogger().LogWarn() << "event=ANALYTICS_FAILED"
                                << ", log_file=" << logFilePath;
    return;
  }

  if (!analytics.WriteReport(reportFilePath)) {
    AnalyticsLogger().LogWarn()
        << "event=ANALYTICS_REPORT_FAILED"
        << ", log_file=" << logFilePath
        << ", report_file=" << reportFilePath;
    return;
  }

  AnalyticsLogger().LogInfo()
      << "event=ANALYTICS_REPORT_WRITTEN"
      << ", log_file=" << logFilePath
      << ", report_file=" << reportFilePath;
}

void SignalControlApplication::RunFsmWorker() noexcept {
  try {
    PrefaultCurrentThreadStack("fsm_worker");

    while (fsmRunning_.load(std::memory_order_acquire)) {
      const SignalDisplay display = signalFsmEngine_.processTick();

      if (!fsmRunning_.load(std::memory_order_acquire)) {
        break;
      }

      outputSimulator_.submit(display);
    }
  } catch (...) {
    fsmFailed_.store(true, std::memory_order_release);
  }
}

void SignalControlApplication::StopFsmWorker() noexcept {
  fsmRunning_.store(false, std::memory_order_release);
  planSyncChannel_.RequestShutdown();

  if (fsmWorker_.joinable()) {
    fsmWorker_.join();
    signalFsmEngine_.dumpWakeupSamplesToLog();
  }
}

TimingPlan SignalControlApplication::CreateCongestionPlan() const {
  TimingPlan plan{};
  plan.planId = kCongestionPlanId;
  plan.greenNorthSouthMs = kShortGreenDurationMs;
  plan.greenEastWestMs = kLongGreenDurationMs;
  plan.yellowMs = kYellowDurationMs;
  plan.allRedMs = kAllRedDurationMs;
  plan.cycleLengthMs = CalculateCycleLengthMs(
      plan.greenNorthSouthMs, plan.greenEastWestMs, plan.yellowMs,
      plan.allRedMs);
  plan.emergencyNorthSouth = false;
  plan.emergencyEastWest = false;
  return plan;
}

TimingPlan SignalControlApplication::CreateEmergencyPlan() const {
  TimingPlan plan{};
  plan.planId = kEmergencyPlanId;
  plan.greenNorthSouthMs = kLongGreenDurationMs;
  plan.greenEastWestMs = kShortGreenDurationMs;
  plan.yellowMs = kYellowDurationMs;
  plan.allRedMs = kAllRedDurationMs;
  plan.cycleLengthMs = CalculateCycleLengthMs(
      plan.greenNorthSouthMs, plan.greenEastWestMs, plan.yellowMs,
      plan.allRedMs);
  plan.emergencyNorthSouth = true;
  plan.emergencyEastWest = false;
  return plan;
}

}  // namespace traffic_signal_controller