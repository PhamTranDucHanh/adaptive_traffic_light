#include "traffic_signal_controller/signal_control_app.h"

#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <cerrno>
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
constexpr int kDefaultLifecyclePriority{70};
constexpr int kDefaultFsmPriority{85};
constexpr std::size_t kPrefaultStackBytes{64U * 1024U};
constexpr std::size_t kPageSize{4096U};

struct SchedulingResult {
  bool success;
  int errorNumber;
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

int GetPriorityFromEnvOrDefault(const char* const name,
                                const int defaultPriority) {
  const char* const value = std::getenv(name);

  if (value == nullptr || value[0] == '\0') {
    return defaultPriority;
  }

  char* end{};
  errno = 0;
  const long parsed = std::strtol(value, &end, 10);

  if (errno != 0 || end == value || *end != '\0') {
    AppLogger().LogWarn() << "event=THREAD_PRIORITY_ENV_INVALID"
                          << ", variable=" << name << ", value=" << value
                          << ", fallback_priority=" << defaultPriority;
    return defaultPriority;
  }

  const int minPriority = sched_get_priority_min(SCHED_FIFO);
  const int maxPriority = sched_get_priority_max(SCHED_FIFO);

  if (parsed < minPriority || parsed > maxPriority) {
    AppLogger().LogWarn() << "event=THREAD_PRIORITY_ENV_INVALID"
                          << ", variable=" << name << ", value=" << value
                          << ", min_priority=" << minPriority
                          << ", max_priority=" << maxPriority
                          << ", fallback_priority=" << defaultPriority;
    return defaultPriority;
  }

  return static_cast<int>(parsed);
}

SchedulingResult SetFifoScheduling(const pthread_t thread,
                                   const int priority) noexcept {
  sched_param parameters{};
  parameters.sched_priority = priority;

  const int result = pthread_setschedparam(thread, SCHED_FIFO, &parameters);
  return SchedulingResult{result == 0, result};
}

void LogSchedulingResult(const char* const threadName, const int priority,
                         const SchedulingResult result) {
  if (result.success) {
    AppLogger().LogInfo() << "event=THREAD_SCHEDULING_CONFIGURED"
                          << ", thread=" << threadName << ", policy=SCHED_FIFO"
                          << ", priority=" << priority;
    return;
  }

  AppLogger().LogWarn() << "event=THREAD_SCHEDULING_FAILED"
                        << ", thread=" << threadName << ", policy=SCHED_FIFO"
                        << ", priority=" << priority
                        << ", error=" << result.errorNumber
                        << ", reason=" << std::strerror(result.errorNumber);
}

void LockProcessMemory() {
  if (::mlockall(MCL_CURRENT | MCL_FUTURE) == 0) {
    AppLogger().LogInfo() << "event=PROCESS_MEMORY_LOCKED"
                          << ", flags=MCL_CURRENT|MCL_FUTURE";
    return;
  }

  const int errorNumber = errno;
  AppLogger().LogWarn() << "event=PROCESS_MEMORY_LOCK_FAILED"
                        << ", flags=MCL_CURRENT|MCL_FUTURE"
                        << ", error=" << errorNumber
                        << ", reason=" << std::strerror(errorNumber);
}

void PrefaultCurrentThreadStack(const char* const threadName) noexcept {
  volatile std::uint8_t stackPages[kPrefaultStackBytes]{};

  for (std::size_t offset{0U}; offset < kPrefaultStackBytes;
       offset += kPageSize) {
    stackPages[offset] = static_cast<std::uint8_t>(0U);
  }

  AppLogger().LogInfo() << "event=THREAD_STACK_PREFAULTED"
                        << ", thread=" << threadName
                        << ", bytes=" << kPrefaultStackBytes;
}

void EnsureRuntimeLogDirectoryExists() {
  (void)mkdir("/tmp/traffic_signal_controller", 0755);
  (void)mkdir("/tmp/traffic_signal_controller/logs", 0755);
}

}  // namespace

SignalControlApplication::~SignalControlApplication() { StopFsmWorker(); }

std::int32_t SignalControlApplication::Initialize(
    const score::mw::lifecycle::ApplicationContext& context) {
  (void)context;

  // The Lifecycle HealthMonitor implementation logs through the Rust logger.
  // Initialize it before HealthReporter starts its background worker, following
  // the lifecycle cpp_supervised_app example.
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

  const int lifecyclePriority = GetPriorityFromEnvOrDefault(
      "TRAFFIC_SIGNAL_CONTROLLER_LIFECYCLE_PRIORITY",
      kDefaultLifecyclePriority);
  LogSchedulingResult("lifecycle_main", lifecyclePriority,
                      SetFifoScheduling(pthread_self(), lifecyclePriority));

  cycleCount_ = 0U;
  demoPlanReceiverState_ = DemoPlanReceiverState::kWaitingForCongestionPlan;

  fsmFailed_.store(false, std::memory_order_release);
  fsmRunning_.store(true, std::memory_order_release);

  try {
    fsmWorker_ = std::thread{&SignalControlApplication::RunFsmWorker, this};
    const int fsmPriority = GetPriorityFromEnvOrDefault(
        "TRAFFIC_SIGNAL_CONTROLLER_FSM_PRIORITY", kDefaultFsmPriority);
    LogSchedulingResult(
        "fsm_worker", fsmPriority,
        SetFifoScheduling(fsmWorker_.native_handle(), fsmPriority));
  } catch (...) {
    fsmRunning_.store(false, std::memory_order_release);
    healthReporter_.shutdown();
    AppLogger().LogWarn() << "event=APP_INIT_FAILED"
                          << ", reason=FSM_WORKER_START_FAILED";
    return EXIT_FAILURE;
  }

  initialized_ = true;
  AppLogger().LogInfo() << "event=APP_READY"
                        << ", period_ms=1000";
  return EXIT_SUCCESS;
}

std::int32_t SignalControlApplication::Run(
    const score::cpp::stop_token& stopToken) {
  if (!initialized_) {
    return EXIT_FAILURE;
  }

  using namespace std::chrono_literals;
  constexpr auto kPeriod = 1s;
  auto nextRelease = std::chrono::steady_clock::now() + kPeriod;
  std::int32_t exitCode{EXIT_SUCCESS};

  AppLogger().LogInfo() << "event=PERIODIC_LOOP_STARTED";
  while (!stopToken.stop_requested()) {
    // Delay the first release by one complete period so the heartbeat monitor
    // observes a real 1-second baseline. Absolute releases avoid timer drift.
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
                          << ", cycle=" << cycleCount_ << ", period_ms=1000";

    // Close the deadline after the useful control work. HealthMonitor combines
    // this result with the heartbeat and reports Alive asynchronously.
    healthReporter_.finishControlCycle();

    nextRelease += kPeriod;
  }

  StopFsmWorker();
  WriteAnalyticsReport();
  healthReporter_.shutdown();
  initialized_ = false;
  AppLogger().LogInfo() << "event=APP_STOPPED"
                        << ", cycles_completed=" << cycleCount_;
  return exitCode;
}

void SignalControlApplication::RunPlanReceiverSimulation() {
  /*
   * Đây là chỗ giả lập input từ module plan receiver/communication.
   * Sau này khi tích hợp thật, module khác chỉ cần gọi
   * PlanReceiver::ReceivePlan() với TimingPlan nhận được từ IPC/network/config
   * pipeline.
   */
  switch (demoPlanReceiverState_) {
    case DemoPlanReceiverState::kWaitingForCongestionPlan: {
      if (cycleCount_ < 5U) {
        return;
      }

      const TimingPlan congestionPlan = CreateCongestionPlan();
      const bool accepted = planReceiver_.ReceivePlan(congestionPlan);

      AppLogger().LogInfo()
          << "event=SIMULATED_PLAN_SUBMITTED"
          << ", cycle=" << cycleCount_ << ", source=simulated_plan_receiver"
          << ", plan_type=congestion"
          << ", result=" << (accepted ? "accepted" : "rejected");

      if (accepted) {
        demoPlanReceiverState_ =
            DemoPlanReceiverState::kWaitingForEmergencyPlan;
      }
      return;
    }

    case DemoPlanReceiverState::kWaitingForEmergencyPlan: {
      if (cycleCount_ < 15U) {
        return;
      }

      const TimingPlan emergencyPlan = CreateEmergencyPlan();
      const bool accepted = planReceiver_.ReceivePlan(emergencyPlan);

      AppLogger().LogInfo()
          << "event=SIMULATED_PLAN_SUBMITTED"
          << ", cycle=" << cycleCount_ << ", source=simulated_plan_receiver"
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
      "TRAFFIC_SIGNAL_CONTROLLER_ANALYTICS_LOG_FILE", kDefaultAnalyticsLogFile);
  const std::string reportFilePath =
      GetEnvOrDefault("TRAFFIC_SIGNAL_CONTROLLER_ANALYTICS_REPORT_FILE",
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
        << ", log_file=" << logFilePath << ", report_file=" << reportFilePath;
    return;
  }

  AnalyticsLogger().LogInfo()
      << "event=ANALYTICS_REPORT_WRITTEN"
      << ", log_file=" << logFilePath << ", report_file=" << reportFilePath;
}

void SignalControlApplication::RunFsmWorker() noexcept {
  try {
    PrefaultCurrentThreadStack("fsm_worker");

    while (fsmRunning_.load(std::memory_order_acquire)) {
      const SignalDisplay display = signalFsmEngine_.processTick();

      if (!fsmRunning_.load(std::memory_order_acquire)) {
        break;
      }

      outputSimulator_.publish(display);
    }
  } catch (...) {
    fsmFailed_.store(true, std::memory_order_release);
  }
}

void SignalControlApplication::StopFsmWorker() noexcept {
  fsmRunning_.store(false, std::memory_order_release);

  // Wake pthread_cond_timedwait() if the FSM is currently waiting in GREEN.
  planSyncChannel_.RequestShutdown();

  if (fsmWorker_.joinable()) {
    fsmWorker_.join();
    signalFsmEngine_.dumpWakeupSamplesToLog();
  }
}

TimingPlan SignalControlApplication::CreateCongestionPlan() const {
  TimingPlan plan{};

  plan.planId = 1001U;
  plan.greenNorthSouthMs = 15'000U;
  plan.greenEastWestMs = 30'000U;
  plan.yellowMs = 3'000U;
  plan.allRedMs = 1'000U;

  plan.cycleLengthMs = plan.greenNorthSouthMs + plan.greenEastWestMs +
                       (2U * plan.yellowMs) + (2U * plan.allRedMs);

  plan.emergencyNorthSouth = false;
  plan.emergencyEastWest = false;

  return plan;
}

TimingPlan SignalControlApplication::CreateEmergencyPlan() const {
  TimingPlan plan{};

  plan.planId = 2001U;
  plan.greenNorthSouthMs = 30'000U;
  plan.greenEastWestMs = 15'000U;
  plan.yellowMs = 3'000U;
  plan.allRedMs = 1'000U;

  plan.cycleLengthMs = plan.greenNorthSouthMs + plan.greenEastWestMs +
                       (2U * plan.yellowMs) + (2U * plan.allRedMs);

  plan.emergencyNorthSouth = true;
  plan.emergencyEastWest = false;

  return plan;
}

}  // namespace traffic_signal_controller
