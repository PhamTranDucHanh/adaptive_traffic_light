#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <thread>

#include "analytics_service/output_simulator.h"
#include "common/logging_contexts.h"
#include "common/plan_sync_channel.h"
#include "score/mw/log/logger.h"
#include "score/mw/log/logging.h"
#include "score/mw/log/rust/stdout_logger_init.h"
#include "traffic_signal_controller/plan_receiver.h"
#include "traffic_signal_controller/signal_fsm_engine.h"

namespace {

score::mw::log::Logger& Logger() {
  static auto& logger = score::mw::log::CreateLogger(ctrl::logging::KCtxDemo,
                                                     "Traffic Signal Demo");
  return logger;
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

  Logger().LogInfo() << "Traffic signal demonstration started";

  std::thread planThread{[&receiver]() {
    using namespace std::chrono_literals;

    /*
     * Scenario dự kiến:
     *
     *  0s: Default NS_GREEN bắt đầu với 30 giây.
     *
     *  5s: Gửi congestion plan.
     *      Normal plan được giữ pending và chưa áp dụng ngay.
     *
     * 13s: NS_GREEN còn khoảng 17 giây.
     *      Emergency NS đúng hướng nhưng ngoài window 5-10 giây.
     *      Kết quả mong đợi: reject.
     *
     * 14s: NS_GREEN còn khoảng 16 giây.
     *      Emergency EW sai hướng.
     *      Kết quả mong đợi: reject.
     *
     * 22s: NS_GREEN còn khoảng 8 giây.
     *      Emergency NS đúng hướng và đúng timing window.
     *      Kết quả mong đợi: accept, NS_GREEN được đặt thành 20 giây.
     *
     * Khoảng 46-48s:
     *      NS emergency kết thúc, FSM chạy YELLOW -> ALL_RED.
     *      Congestion pending được áp dụng tại cuối ALL_RED.
     *
     * Khoảng 49s: FSM đang EW_GREEN của congestion plan.
     *      Emergency NS sai hướng.
     *      Kết quả mong đợi: reject.
     *
     * Khoảng 50s: EW_GREEN còn trong window 5-10 giây.
     *      Emergency EW đúng hướng.
     *      Kết quả mong đợi: accept, EW_GREEN được đặt thành 20 giây.
     */
 
    // Case 1: normal congestion plan được giữ pending.
    std::this_thread::sleep_for(5s);
    SendPlanAndLog(receiver, CreateCongestionPlan(),
                   "CASE_1_CONGESTION_PENDING");

    // Case 2: đúng hướng nhưng quá sớm, remaining khoảng 17 giây.
    std::this_thread::sleep_for(8s);
    SendPlanAndLog(receiver,
                   CreateEmergencyPlan(EmergencyDirection::NORTH_SOUTH, 2001U),
                   "CASE_2_NS_TOO_EARLY");

    // Case 3: sai hướng trong khi phase hiện tại là NS_GREEN.
    std::this_thread::sleep_for(1s);
    SendPlanAndLog(receiver,
                   CreateEmergencyPlan(EmergencyDirection::EAST_WEST, 2002U),
                   "CASE_3_EW_DURING_NS_GREEN");

    // Case 4: emergency NS hợp lệ, remaining khoảng 8 giây.
    std::this_thread::sleep_for(8s);
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

  /*
   * TIMER_INTERVAL_MS phải là 1'000U.
   *
   * Chạy 80 tick = khoảng 80 giây để quan sát đầy đủ:
   * - default plan;
   * - congestion pending;
   * - emergency NS reject/accept;
   * - trở về chu kỳ bình thường;
   * - congestion được áp dụng;
   * - emergency EW reject/accept;
   * - FSM tiếp tục sau emergency EW.
   */
  constexpr std::uint32_t kDemoDurationSeconds{80U};
  constexpr std::uint32_t kDemoTicks{kDemoDurationSeconds};

  for (std::uint32_t tick{0U}; tick < kDemoTicks; ++tick) {
    const SignalDisplay display = fsm.processTick();
    output.publish(display);
  }

  syncChannel.RequestShutdown();

  if (planThread.joinable()) {
    planThread.join();
  }

  Logger().LogInfo() << "Traffic signal demonstration completed";

  return EXIT_SUCCESS;
}