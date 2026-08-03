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
#include <fstream>
#include <iostream>
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
constexpr const char* kRuntimeAnalyticsOutputDirectory{
    "/tmp/traffic_signal_controller/logs/output"};

constexpr std::int32_t kDefaultFsmPriority{80};
constexpr std::int32_t kDefaultPlanReceiverPriority{70};
constexpr std::int32_t kTrafficSignalControllerCpu{3};
constexpr std::int32_t kDecimalBase{10};

constexpr std::size_t kBytesPerKibibyte{1024U};
constexpr std::size_t kPrefaultStackSizeKibibytes{64U};
constexpr std::size_t kPrefaultStackBytes{kPrefaultStackSizeKibibytes *
                                          kBytesPerKibibyte};
constexpr std::size_t kPageSizeBytes{4096U};

constexpr mode_t kRuntimeDirectoryPermissions{0755};
constexpr std::uint64_t kControlPeriodMilliseconds{1000U};

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

std::int32_t GetIntegerFromEnvOrDefault(const char* const name,
                                        const std::int32_t defaultValue) {
  const char* const value = std::getenv(name);

  if (value == nullptr || value[0] == '\0') {
    return defaultValue;
  }

  std::int32_t parsedValue{};
  const char* const valueEnd = value + std::strlen(value);
  const auto parseResult =
      std::from_chars(value, valueEnd, parsedValue, kDecimalBase);

  if (parseResult.ec != std::errc{} || parseResult.ptr != valueEnd) {
    AppLogger().LogWarn() << "event=INTEGER_ENV_INVALID"
                          << ", variable=" << name << ", value=" << value
                          << ", fallback=" << defaultValue;
    return defaultValue;
  }

  return parsedValue;
}

std::int32_t GetPriorityFromEnvOrDefault(const char* const name,
                                         const std::int32_t defaultPriority) {
  const std::int32_t parsedPriority =
      GetIntegerFromEnvOrDefault(name, defaultPriority);

  const std::int32_t minimumPriority =
      static_cast<std::int32_t>(sched_get_priority_min(SCHED_FIFO));
  const std::int32_t maximumPriority =
      static_cast<std::int32_t>(sched_get_priority_max(SCHED_FIFO));

  if (parsedPriority < minimumPriority || parsedPriority > maximumPriority) {
    AppLogger().LogWarn() << "event=THREAD_PRIORITY_ENV_INVALID"
                          << ", variable=" << name
                          << ", value=" << parsedPriority
                          << ", min_priority=" << minimumPriority
                          << ", max_priority=" << maximumPriority
                          << ", fallback_priority=" << defaultPriority;
    return defaultPriority;
  }

  return parsedPriority;
}

bool PinCurrentThreadToCpu(const std::int32_t cpu,
                           const char* const threadName) noexcept {
  cpu_set_t cpuSet{};
  CPU_ZERO(&cpuSet);
  CPU_SET(cpu, &cpuSet);

  const std::int32_t result = static_cast<std::int32_t>(
      pthread_setaffinity_np(pthread_self(), sizeof(cpuSet), &cpuSet));

  if (result != EXIT_SUCCESS) {
    AppLogger().LogWarn() << "event=THREAD_AFFINITY_FAILED"
                          << ", thread=" << threadName << ", cpu=" << cpu
                          << ", error=" << result
                          << ", reason=" << std::strerror(result);
    return false;
  }

  AppLogger().LogInfo() << "event=THREAD_AFFINITY_CONFIGURED"
                        << ", thread=" << threadName << ", cpu=" << cpu;
  return true;
}

bool ConfigureRealtimeThreadAttributes(pthread_attr_t& attributes,
                                       const std::int32_t priority,
                                       const std::int32_t cpu,
                                       const char* const threadName) noexcept {
  std::int32_t result =
      pthread_attr_setinheritsched(&attributes, PTHREAD_EXPLICIT_SCHED);
  if (result != EXIT_SUCCESS) {
    AppLogger().LogWarn() << "event=THREAD_ATTRIBUTE_FAILED"
                          << ", thread=" << threadName
                          << ", operation=pthread_attr_setinheritsched"
                          << ", error=" << result
                          << ", reason=" << std::strerror(result);
    return false;
  }

  result = pthread_attr_setschedpolicy(&attributes, SCHED_FIFO);
  if (result != EXIT_SUCCESS) {
    AppLogger().LogWarn() << "event=THREAD_ATTRIBUTE_FAILED"
                          << ", thread=" << threadName
                          << ", operation=pthread_attr_setschedpolicy"
                          << ", error=" << result
                          << ", reason=" << std::strerror(result);
    return false;
  }

  sched_param schedulingParameters{};
  schedulingParameters.sched_priority = static_cast<int>(priority);

  result = pthread_attr_setschedparam(&attributes, &schedulingParameters);
  if (result != EXIT_SUCCESS) {
    AppLogger().LogWarn() << "event=THREAD_ATTRIBUTE_FAILED"
                          << ", thread=" << threadName
                          << ", operation=pthread_attr_setschedparam"
                          << ", error=" << result
                          << ", reason=" << std::strerror(result);
    return false;
  }

  cpu_set_t cpuSet{};
  CPU_ZERO(&cpuSet);
  CPU_SET(cpu, &cpuSet);

  result = pthread_attr_setaffinity_np(&attributes, sizeof(cpuSet), &cpuSet);
  if (result != EXIT_SUCCESS) {
    AppLogger().LogWarn() << "event=THREAD_ATTRIBUTE_FAILED"
                          << ", thread=" << threadName
                          << ", operation=pthread_attr_setaffinity_np"
                          << ", cpu=" << cpu << ", error=" << result
                          << ", reason=" << std::strerror(result);
    return false;
  }

  return true;
}

void LockProcessMemory() {
  if (::mlockall(MCL_CURRENT | MCL_FUTURE) == EXIT_SUCCESS) {
    AppLogger().LogInfo() << "event=PROCESS_MEMORY_LOCKED"
                          << ", flags=MCL_CURRENT|MCL_FUTURE";
    return;
  }

  const std::int32_t errorNumber = static_cast<std::int32_t>(errno);
  AppLogger().LogWarn() << "event=PROCESS_MEMORY_LOCK_FAILED"
                        << ", flags=MCL_CURRENT|MCL_FUTURE"
                        << ", error=" << errorNumber << ", reason="
                        << std::strerror(static_cast<int>(errorNumber));
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

bool TruncateFile(const std::string& path) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  return output.is_open();
}

void ResetAnalyticsOutputFiles() {
  EnsureRuntimeLogDirectoryExists();

  const std::string logFilePath = GetEnvOrDefault(
      "TRAFFIC_SIGNAL_CONTROLLER_ANALYTICS_LOG_FILE", kDefaultAnalyticsLogFile);
  const std::string reportFilePath =
      GetEnvOrDefault("TRAFFIC_SIGNAL_CONTROLLER_ANALYTICS_REPORT_FILE",
                      kDefaultAnalyticsReportFile);

  if (!TruncateFile(logFilePath)) {
    std::cerr << "Unable to reset analytics log file: " << logFilePath << '\n';
  }

  if (!TruncateFile(logFilePath + ".txt")) {
    std::cerr << "Unable to reset converted analytics log file: "
              << logFilePath << ".txt\n";
  }

  if (!TruncateFile(reportFilePath)) {
    std::cerr << "Unable to reset analytics report file: " << reportFilePath
              << '\n';
  }
}

}  // namespace

SignalControlApplication::~SignalControlApplication() {
  StopPlanReceiverWorker();
  StopFsmWorker();
  outputSimulator_.stop();
}

std::int32_t SignalControlApplication::Initialize(
    const score::mw::lifecycle::ApplicationContext& context) {
  (void)context;

  ResetAnalyticsOutputFiles();

  score::mw::log::rust::StdoutLoggerBuilder loggerBuilder;
  loggerBuilder.Context("TSIG")
      .LogLevel(score::mw::log::rust::LogLevel::Verbose)
      .SetAsDefaultLogger();

  // Pin lifecycle_main before starting any application-owned worker.
  // Every worker created afterwards inherits this CPU3 affinity.
  if (!PinCurrentThreadToCpu(kTrafficSignalControllerCpu, "lifecycle_main")) {
    return EXIT_FAILURE;
  }

  LockProcessMemory();
  PrefaultCurrentThreadStack("lifecycle_main");

  if (!healthReporter_.initialize()) {
    AppLogger().LogWarn() << "event=APP_INIT_FAILED"
                          << ", reason=HEALTH_REPORTER_INIT_FAILED";
    return EXIT_FAILURE;
  }

  cycleCount_ = 0U;
  fsmFailed_.store(false, std::memory_order_release);
  planReceiverFailed_.store(false, std::memory_order_release);
  fsmRunning_.store(true, std::memory_order_release);
  planReceiverRunning_.store(true, std::memory_order_release);

  try {
    outputSimulator_.start();
  } catch (...) {
    fsmRunning_.store(false, std::memory_order_release);
    planReceiverRunning_.store(false, std::memory_order_release);
    healthReporter_.shutdown();
    AppLogger().LogWarn() << "event=APP_INIT_FAILED"
                          << ", reason=OUTPUT_SIMULATOR_START_FAILED";
    return EXIT_FAILURE;
  }

  if (!StartPlanReceiverWorker()) {
    fsmRunning_.store(false, std::memory_order_release);
    planReceiverRunning_.store(false, std::memory_order_release);
    outputSimulator_.stop();
    healthReporter_.shutdown();
    AppLogger().LogWarn() << "event=APP_INIT_FAILED"
                          << ", reason=PLAN_RECEIVER_THREAD_CREATE_FAILED";
    return EXIT_FAILURE;
  }

  if (!StartFsmWorker()) {
    fsmRunning_.store(false, std::memory_order_release);
    StopPlanReceiverWorker();
    planSyncChannel_.RequestShutdown();
    outputSimulator_.stop();
    healthReporter_.shutdown();
    AppLogger().LogWarn() << "event=APP_INIT_FAILED"
                          << ", reason=FSM_THREAD_CREATE_FAILED";
    return EXIT_FAILURE;
  }

  initialized_ = true;

  AppLogger().LogInfo() << "event=APP_READY"
                        << ", application_threads=4"
                        << ", cpu=" << kTrafficSignalControllerCpu
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

    if (fsmFailed_.load(std::memory_order_acquire)) {
      AppLogger().LogWarn() << "event=CONTROL_CYCLE_FAILED"
                            << ", reason=FSM_WORKER_FAILED";
      exitCode = EXIT_FAILURE;
    } else if (planReceiverFailed_.load(std::memory_order_acquire)) {
      AppLogger().LogWarn() << "event=CONTROL_CYCLE_FAILED"
                            << ", reason=PLAN_RECEIVER_WORKER_FAILED";
      exitCode = EXIT_FAILURE;
    }

    healthReporter_.finishControlCycle();

    if (exitCode != EXIT_SUCCESS) {
      break;
    }

    ++cycleCount_;
    AppLogger().LogInfo() << "event=CONTROL_CYCLE_COMPLETED"
                          << ", cycle=" << cycleCount_
                          << ", period_ms=" << kControlPeriodMilliseconds;

    // Drain from the non-real-time lifecycle thread.
    signalFsmEngine_.flushWakeupSamplesToLog();

    nextRelease += kControlPeriod;

    // Do not execute catch-up lifecycle/health cycles back-to-back after a
    // delayed iteration. The FSM has its own real-time worker, so replaying a
    // missed lifecycle release adds no control value and would submit multiple
    // heartbeats inside one S-CORE HealthMonitor evaluation window.
    const auto releaseCheckTime = std::chrono::steady_clock::now();
    if (nextRelease <= releaseCheckTime) {
      nextRelease = releaseCheckTime + kControlPeriod;
      AppLogger().LogWarn() << "event=CONTROL_RELEASE_RESYNCHRONIZED"
                            << ", reason=MISSED_LIFECYCLE_RELEASE"
                            << ", next_period_ms="
                            << kControlPeriodMilliseconds;
    }
  }

  StopPlanReceiverWorker();
  StopFsmWorker();
  outputSimulator_.stop();
  WriteAnalyticsReport();
  healthReporter_.shutdown();
  initialized_ = false;

  AppLogger().LogInfo() << "event=APP_STOPPED"
                        << ", cycles_completed=" << cycleCount_;
  return exitCode;
}

void* SignalControlApplication::FsmWorkerEntry(void* const argument) noexcept {
  if (argument == nullptr) {
    return nullptr;
  }

  static_cast<SignalControlApplication*>(argument)->RunFsmWorker();
  return nullptr;
}

void* SignalControlApplication::PlanReceiverWorkerEntry(
    void* const argument) noexcept {
  if (argument == nullptr) {
    return nullptr;
  }

  static_cast<SignalControlApplication*>(argument)->RunPlanReceiverWorker();
  return nullptr;
}

bool SignalControlApplication::StartFsmWorker() noexcept {
  std::int32_t priority = GetPriorityFromEnvOrDefault(
      "TRAFFIC_SIGNAL_CONTROLLER_FSM_PRIORITY", kDefaultFsmPriority);
  if (priority <= kDefaultPlanReceiverPriority) {
    AppLogger().LogWarn() << "event=FSM_PRIORITY_INVALID"
                          << ", configured_priority=" << priority
                          << ", receiver_priority="
                          << kDefaultPlanReceiverPriority
                          << ", fallback_priority=" << kDefaultFsmPriority;
    priority = kDefaultFsmPriority;
  }
  constexpr std::int32_t cpu{kTrafficSignalControllerCpu};

  pthread_attr_t attributes{};
  std::int32_t result = pthread_attr_init(&attributes);
  if (result != EXIT_SUCCESS) {
    return false;
  }

  if (!ConfigureRealtimeThreadAttributes(attributes, priority, cpu,
                                         "fsm_worker")) {
    (void)pthread_attr_destroy(&attributes);
    return false;
  }

  result = pthread_create(&fsmWorker_, &attributes,
                          &SignalControlApplication::FsmWorkerEntry, this);
  (void)pthread_attr_destroy(&attributes);

  if (result != EXIT_SUCCESS) {
    AppLogger().LogWarn() << "event=FSM_THREAD_CREATE_FAILED"
                          << ", error=" << result
                          << ", reason=" << std::strerror(result);
    return false;
  }

  fsmWorkerCreated_ = true;
  AppLogger().LogInfo() << "event=FSM_THREAD_CREATED"
                        << ", policy=SCHED_FIFO"
                        << ", priority=" << priority << ", cpu=" << cpu;
  return true;
}

bool SignalControlApplication::StartPlanReceiverWorker() noexcept {
  constexpr std::int32_t priority{kDefaultPlanReceiverPriority};
  constexpr std::int32_t cpu{kTrafficSignalControllerCpu};

  pthread_attr_t attributes{};
  std::int32_t result = pthread_attr_init(&attributes);
  if (result != EXIT_SUCCESS) {
    return false;
  }

  if (!ConfigureRealtimeThreadAttributes(attributes, priority, cpu,
                                         "plan_receiver")) {
    (void)pthread_attr_destroy(&attributes);
    return false;
  }

  result =
      pthread_create(&planReceiverWorker_, &attributes,
                     &SignalControlApplication::PlanReceiverWorkerEntry, this);
  (void)pthread_attr_destroy(&attributes);

  if (result != EXIT_SUCCESS) {
    AppLogger().LogWarn() << "event=PLAN_RECEIVER_THREAD_CREATE_FAILED"
                          << ", error=" << result
                          << ", reason=" << std::strerror(result);
    return false;
  }

  planReceiverWorkerCreated_ = true;
  AppLogger().LogInfo() << "event=PLAN_RECEIVER_THREAD_CREATED"
                        << ", policy=SCHED_FIFO"
                        << ", priority=" << priority << ", cpu=" << cpu;
  return true;
}

void SignalControlApplication::RunFsmWorker() noexcept {
  try {
    (void)pthread_setname_np(pthread_self(), "tsc_fsm");
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

void SignalControlApplication::RunPlanReceiverWorker() noexcept {
  try {
    (void)pthread_setname_np(pthread_self(), "tsc_plan_rx");
    PrefaultCurrentThreadStack("plan_receiver");
    if (!mqTimingPlanReceiverWorker_.Run(planReceiverRunning_)) {
      planReceiverFailed_.store(true, std::memory_order_release);
    }
  } catch (...) {
    planReceiverFailed_.store(true, std::memory_order_release);
  }
}

void SignalControlApplication::StopPlanReceiverWorker() noexcept {
  planReceiverRunning_.store(false, std::memory_order_release);

  if (!planReceiverWorkerCreated_) {
    return;
  }

  const std::int32_t result =
      static_cast<std::int32_t>(pthread_join(planReceiverWorker_, nullptr));
  if (result != EXIT_SUCCESS) {
    AppLogger().LogWarn() << "event=PLAN_RECEIVER_THREAD_JOIN_FAILED"
                          << ", error=" << result
                          << ", reason=" << std::strerror(result);
  }

  planReceiverWorkerCreated_ = false;
}

void SignalControlApplication::StopFsmWorker() noexcept {
  fsmRunning_.store(false, std::memory_order_release);
  planSyncChannel_.RequestShutdown();

  if (!fsmWorkerCreated_) {
    return;
  }

  const std::int32_t result =
      static_cast<std::int32_t>(pthread_join(fsmWorker_, nullptr));
  if (result != EXIT_SUCCESS) {
    AppLogger().LogWarn() << "event=FSM_THREAD_JOIN_FAILED"
                          << ", error=" << result
                          << ", reason=" << std::strerror(result);
  }

  fsmWorkerCreated_ = false;
  signalFsmEngine_.dumpWakeupSamplesToLog();
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

  std::string archivedInputPath;
  if (!analytics.ArchiveInputData(kRuntimeAnalyticsOutputDirectory,
                                  archivedInputPath)) {
    AnalyticsLogger().LogWarn()
        << "event=ANALYTICS_INPUT_ARCHIVE_FAILED"
        << ", log_file=" << logFilePath
        << ", output_directory=" << kRuntimeAnalyticsOutputDirectory;
  } else {
    AnalyticsLogger().LogInfo()
        << "event=ANALYTICS_INPUT_ARCHIVED"
        << ", log_file=" << logFilePath
        << ", archived_input=" << archivedInputPath;
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

}  // namespace traffic_signal_controller
