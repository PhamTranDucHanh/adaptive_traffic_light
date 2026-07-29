#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>

#include <cerrno>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "analytics_service/analytics.h"
#include "analytics_service/output_simulator.h"
#include "common/logging_contexts.h"
#include "common/plan_sync_channel.h"
#include "common/spsc_queue.h"
#include "score/mw/log/logger.h"
#include "score/mw/log/logging.h"
#include "score/mw/log/rust/stdout_logger_init.h"
#include "traffic_signal_controller/plan_receiver.h"
#include "traffic_signal_controller/signal_fsm_engine.h"
namespace {

score::mw::log::Logger& Logger() {
  static auto& logger = score::mw::log::CreateLogger(ctrl::logging::kCtxDemo,
                                                     "Traffic Signal Demo");
  return logger;
}


struct SchedulingResult final {
  bool success;
  int errorNumber;
};

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
    Logger().LogInfo() << "event=THREAD_SCHEDULING_CONFIGURED"
                       << ", thread=" << threadName
                       << ", policy=SCHED_FIFO"
                       << ", priority=" << priority;
    return;
  }

  Logger().LogWarn() << "event=THREAD_SCHEDULING_FAILED"
                     << ", thread=" << threadName
                     << ", policy=SCHED_FIFO"
                     << ", priority=" << priority
                     << ", error=" << result.errorNumber
                     << ", reason=" << std::strerror(result.errorNumber);
}

bool LockProcessMemory() noexcept {
  return ::mlockall(MCL_CURRENT | MCL_FUTURE) == 0;
}

void PrefaultCurrentThreadStack() noexcept {
  constexpr std::size_t kPrefaultBytes{64U * 1024U};
  constexpr std::size_t kPageSize{4096U};

  volatile std::uint8_t stackPages[kPrefaultBytes]{};

  for (std::size_t offset{0U}; offset < kPrefaultBytes;
       offset += kPageSize) {
    stackPages[offset] = static_cast<std::uint8_t>(0U);
  }
}

enum class EmergencyDirection : std::uint8_t { NORTH_SOUTH, EAST_WEST };

TimingPlan CreateCongestionPlan() {
  TimingPlan plan{};

  plan.planId = 1001U;

  plan.greenNorthSouthMs = 5'000U;
  plan.greenEastWestMs = 10'000U;
  plan.yellowMs = 2'000U;
  plan.allRedMs = 1'000U;

  plan.cycleLengthMs = plan.greenNorthSouthMs + plan.greenEastWestMs +
                       (2U * plan.yellowMs) + (2U * plan.allRedMs);

  plan.emergencyNorthSouth = false;
  plan.emergencyEastWest = false;

  return plan;
}

TimingPlan CreateEmergencyPlan(const EmergencyDirection direction,
                               const std::uint32_t planId) {
  TimingPlan plan{};

  plan.planId = planId;

  /*
   * Các timing này giúp request hợp lệ qua PlanReceiver.
   * SignalFSMEngine không dùng chúng để thay currentPlan_.
   *
   * Khi request được FSM chấp nhận, FSM chỉ kéo dài GREEN
   * hiện tại lên 20 giây.
   */
  plan.greenNorthSouthMs = 10'000U;
  plan.greenEastWestMs = 10'000U;
  plan.yellowMs = 2'000U;
  plan.allRedMs = 1'000U;

  plan.cycleLengthMs = plan.greenNorthSouthMs + plan.greenEastWestMs +
                       (2U * plan.yellowMs) + (2U * plan.allRedMs);

  plan.emergencyNorthSouth = direction == EmergencyDirection::NORTH_SOUTH;
  plan.emergencyEastWest = direction == EmergencyDirection::EAST_WEST;

  return plan;
}

void SendPlanAndLog(PlanReceiver& receiver, const TimingPlan& plan,
                    const char* const scenarioName) {
  const bool submitted = receiver.ReceivePlan(plan);

  /*
   * submitted chỉ cho biết PlanReceiver/channel đã nhận request.
   * Kết quả FSM áp dụng hay reject phải xem log evaluate trong FSM.
   */
  Logger().LogInfo() << "Scenario request"
                     << "; name=" << scenarioName << "; planId=" << plan.planId
                     << "; submitted=" << submitted;
}

void RunAnalytics() {
  constexpr const char* kLogFilePath{"/tmp/CTRL.dlt"};
  constexpr const char* kReportFilePath{
      "/tmp/traffic_signal_analytics_report.txt"};

  Analytics analytics{kLogFilePath};

  if (!analytics.Analyze()) {
    Logger().LogWarn() << "event=ANALYTICS_FAILED"
                       << ", log_file=" << kLogFilePath;
    return;
  }

  if (!analytics.WriteReport(kReportFilePath)) {
    Logger().LogWarn() << "event=ANALYTICS_REPORT_FAILED"
                       << ", report_file=" << kReportFilePath;
    return;
  }

  Logger().LogInfo() << "event=ANALYTICS_REPORT_WRITTEN"
                     << ", log_file=" << kLogFilePath
                     << ", report_file=" << kReportFilePath;
}

}  // namespace

int main() {
  score::mw::log::rust::StdoutLoggerBuilder loggerBuilder;

  loggerBuilder.Context("TDEM")
      .LogLevel(score::mw::log::rust::LogLevel::Verbose)
      .SetAsDefaultLogger();

  PlanSyncChannel syncChannel{};
  PlanReceiver receiver{syncChannel};
  SignalFSMEngine fsm{syncChannel};
  OutputSimulator output{};

  constexpr std::size_t kOutputQueueCapacity{128U};
  ctrl::concurrency::SpscQueue<SignalDisplay, kOutputQueueCapacity>
      outputQueue{};
  std::atomic_bool fsmFinished{false};
  std::atomic_bool workersMayStart{false};
  std::uint32_t droppedOutputMessages{0U};

  constexpr int kFsmPriority{80};
  constexpr int kPlanPriority{70};
  constexpr int kOutputPriority{60};

  Logger().LogInfo() << "Traffic signal demonstration started";

  // Start the OutputSimulator's internal non-realtime worker before any
  // SignalDisplay values are submitted by the bridge thread.
  output.start();

  std::thread outputThread{
      [&output, &outputQueue, &fsmFinished, &workersMayStart]() {
    using namespace std::chrono_literals;

    while (!workersMayStart.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }

    PrefaultCurrentThreadStack();
    SignalDisplay display{};

    while (!fsmFinished.load(std::memory_order_acquire) ||
           !outputQueue.Empty()) {
      if (outputQueue.TryPop(display)) {
        output.submit(display);
        continue;
      }

      // Chỉ output thread chờ; FSM producer không bao giờ bị block.
      std::this_thread::sleep_for(1ms);
    }
  }};

  std::thread planThread{[&receiver, &workersMayStart]() {
    using namespace std::chrono_literals;

    while (!workersMayStart.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }

    PrefaultCurrentThreadStack();

    // Case 1: normal congestion plan được giữ pending.
    std::this_thread::sleep_for(5s);
    SendPlanAndLog(receiver, CreateCongestionPlan(),
                   "CASE_1_CONGESTION_PENDING");

    // Case 2: đúng hướng nhưng quá sớm, remaining khoảng 17 giây.
    std::this_thread::sleep_for(8s);
    SendPlanAndLog(receiver,
                   CreateEmergencyPlan(EmergencyDirection::NORTH_SOUTH, 2001U),
                   "CASE_2_NS_TOO_EARLY");

    // Case 3: sai hướng trong khi NS_GREEN còn khoảng 8 giây.
    std::this_thread::sleep_for(8s);
    SendPlanAndLog(receiver,
                   CreateEmergencyPlan(EmergencyDirection::EAST_WEST, 2002U),
                   "CASE_3_EW_DURING_NS_GREEN");

    // Case 4: emergency NS hợp lệ, remaining khoảng 7 giây.
    std::this_thread::sleep_for(1s);
    SendPlanAndLog(receiver,
                   CreateEmergencyPlan(EmergencyDirection::NORTH_SOUTH, 2003U),
                   "CASE_4_VALID_NS_EMERGENCY");

    /*
     * Chờ NS emergency chạy hết, qua YELLOW và ALL_RED,
     * sau đó vào EW_GREEN của congestion plan.
     *
     * Thread scheduling có thể làm lệch khoảng 1 giây.
     */
    std::this_thread::sleep_for(27s);

    // Case 5: sai hướng trong khi phase hiện tại là EW_GREEN.
    SendPlanAndLog(receiver,
                   CreateEmergencyPlan(EmergencyDirection::NORTH_SOUTH, 2004U),
                   "CASE_5_NS_DURING_EW_GREEN");

    // Case 6: emergency EW hợp lệ trong timing window.
    std::this_thread::sleep_for(1s);
    SendPlanAndLog(receiver,
                   CreateEmergencyPlan(EmergencyDirection::EAST_WEST, 2005U),
                   "CASE_6_VALID_EW_EMERGENCY");

    Logger().LogInfo() << "All scenario requests have been sent";
  }};

  if (LockProcessMemory()) {
    Logger().LogInfo() << "event=PROCESS_MEMORY_LOCKED"
                       << ", flags=MCL_CURRENT|MCL_FUTURE";
  } else {
    const int errorNumber = errno;
    Logger().LogWarn() << "event=PROCESS_MEMORY_LOCK_FAILED"
                       << ", error=" << errorNumber
                       << ", reason=" << std::strerror(errorNumber);
  }

  const SchedulingResult outputScheduling =
      SetFifoScheduling(outputThread.native_handle(), kOutputPriority);
  const SchedulingResult planScheduling =
      SetFifoScheduling(planThread.native_handle(), kPlanPriority);

  LogSchedulingResult("OUTPUT", kOutputPriority, outputScheduling);
  LogSchedulingResult("PLAN", kPlanPriority, planScheduling);

  /*
   * Main thread becomes the highest-priority FSM thread. Do this after startup
   * logging so the RT path does not perform logger formatting.
   */
  PrefaultCurrentThreadStack();
  const SchedulingResult fsmScheduling =
      SetFifoScheduling(pthread_self(), kFsmPriority);

  workersMayStart.store(true, std::memory_order_release);

  constexpr std::uint32_t kDemoDurationSeconds{80U};
  constexpr std::uint32_t kDemoTicks{kDemoDurationSeconds};

  // Main thread hiện đóng vai trò FSM thread.
  // Không gọi logger hoặc console output trực tiếp trong vòng lặp này.
  // Dùng absolute sleep để mỗi tick xảy ra đúng chu kỳ 1 giây và tránh
  // tích lũy drift từ execution time của tick trước.
  using FsmClock = std::chrono::steady_clock;
  constexpr auto kFsmPeriod = std::chrono::seconds{1};
  auto nextTick = FsmClock::now();

  for (std::uint32_t tick{0U}; tick < kDemoTicks; ++tick) {
    std::this_thread::sleep_until(nextTick);
    nextTick += kFsmPeriod;

    const SignalDisplay display = fsm.processTick();

    if (!outputQueue.TryPush(display)) {
      ++droppedOutputMessages;
    }
  }

  fsmFinished.store(true, std::memory_order_release);
  syncChannel.RequestShutdown();

  if (planThread.joinable()) {
    planThread.join();
  }

  if (outputThread.joinable()) {
    outputThread.join();
  }

  // All queued displays have now been forwarded through submit(). Stop the
  // internal OutputSimulator worker only after the bridge thread is drained.
  output.stop();

  LogSchedulingResult("FSM", kFsmPriority, fsmScheduling);

  if (droppedOutputMessages > 0U) {
    Logger().LogWarn() << "event=OUTPUT_MESSAGES_DROPPED"
                       << ", count=" << droppedOutputMessages;
  }

  /*
   * Lúc này FSM loop đã dừng, nên có thể format và ghi
   * toàn bộ wakeup samples ra DLT ở non-RT context.
   */
  fsm.dumpWakeupSamplesToLog();

  Logger().LogInfo() << "Traffic signal demonstration completed";

  RunAnalytics();

  return EXIT_SUCCESS;
}