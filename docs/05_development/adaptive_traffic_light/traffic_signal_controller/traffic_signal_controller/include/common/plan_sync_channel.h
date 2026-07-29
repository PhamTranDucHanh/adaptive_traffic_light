#ifndef TRAFFIC_SIGNAL_CONTROLLER_COMMON_PLAN_SYNC_CHANNEL_H_
#define TRAFFIC_SIGNAL_CONTROLLER_COMMON_PLAN_SYNC_CHANNEL_H_

#include <pthread.h>
#include <time.h>

#include "common/config.h"

class PlanSyncChannel final {
 public:
  enum class WaitResult {
    EMERGENCY_AVAILABLE,
    TIMEOUT,
    ERROR,
  };

  PlanSyncChannel();

  PlanSyncChannel(const PlanSyncChannel&) = delete;
  PlanSyncChannel& operator=(const PlanSyncChannel&) = delete;
  PlanSyncChannel(PlanSyncChannel&&) = delete;
  PlanSyncChannel& operator=(PlanSyncChannel&&) = delete;

  ~PlanSyncChannel();

  // FSM gọi khi bắt đầu một phase mới.
  void SetCurrentPhase(PhaseId phaseId);

  /*
   * PlanReceiver gọi sau khi plan đã được validate và translate.
   *
   * Normal plan:
   *   - Luôn được lưu.
   *   - FSM lấy tại ALL_RED.
   *
   * Emergency plan:
   *   - Chỉ được nhận khi FSM đang NS_GREEN hoặc EW_GREEN.
   *   - Đánh thức FSM đang chờ condition variable.
   *
   * Return:
   *   true  -> plan được lưu.
   *   false -> emergency bị loại vì FSM không ở GREEN.
   */
  bool PublishPlan(const PlanData& plan);

  /*
   * FSM gọi trong GREEN.
   *
   * Hàm sử dụng pthread_cond_timedwait() với absolute deadline
   * dựa trên CLOCK_MONOTONIC.
   *
   * Khi trả về EMERGENCY_AVAILABLE, outPlan chứa emergency plan.
   */
  WaitResult WaitForEmergencyUntil(const timespec& absoluteDeadline,
                                   PlanData& outPlan);

  /*
   * FSM gọi tại đầu ALL_RED để lấy normal timing plan mới nhất.
   *
   * Return:
   *   true  -> có pending plan, outPlan được cập nhật.
   *   false -> không có pending plan.
   */
  bool ConsumePendingPlan(PlanData& outPlan);

  // Dùng khi shutdown để đánh thức thread đang wait.
  void RequestShutdown();

 private:
  static bool IsGreenPhase(PhaseId phaseId);
  static bool IsEmergencyPlan(const PlanData& plan);

  pthread_mutex_t mutex_{};
  pthread_cond_t condition_{};

  PhaseId currentPhase_{PhaseId::ALL_RED};

  PlanData pendingNormalPlan_{};
  bool hasPendingNormal_{false};

  PlanData pendingEmergencyPlan_{};
  bool hasPendingEmergency_{false};

  bool shutdownRequested_{false};
};

#endif  // TRAFFIC_SIGNAL_CONTROLLER_COMMON_PLAN_SYNC_CHANNEL_H_