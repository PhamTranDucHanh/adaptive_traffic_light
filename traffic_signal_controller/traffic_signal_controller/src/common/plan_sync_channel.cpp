#include "common/plan_sync_channel.h"

#include <cerrno>
#include <stdexcept>

PlanSyncChannel::PlanSyncChannel() {
  const int mutexResult = pthread_mutex_init(&mutex_, nullptr);

  if (mutexResult != 0) {
    throw std::runtime_error("Failed to initialize PlanSyncChannel mutex");
  }

  pthread_condattr_t conditionAttributes{};

  int result = pthread_condattr_init(&conditionAttributes);

  if (result != 0) {
    pthread_mutex_destroy(&mutex_);

    throw std::runtime_error("Failed to initialize condition attributes");
  }

  /*
   * pthread_cond_timedwait() mặc định thường dùng
   * CLOCK_REALTIME.
   *
   * FSM hệ thống này dùng CLOCK_MONOTONIC nên condition
   * variable cũng phải được cấu hình cùng clock.
   */
  result = pthread_condattr_setclock(&conditionAttributes, CLOCK_MONOTONIC);

  if (result != 0) {
    pthread_condattr_destroy(&conditionAttributes);
    pthread_mutex_destroy(&mutex_);

    throw std::runtime_error("Failed to set condition variable clock");
  }

  result = pthread_cond_init(&condition_, &conditionAttributes);

  pthread_condattr_destroy(&conditionAttributes);

  if (result != 0) {
    pthread_mutex_destroy(&mutex_);

    throw std::runtime_error(
        "Failed to initialize PlanSyncChannel condition variable");
  }
}

PlanSyncChannel::~PlanSyncChannel() {
  pthread_cond_destroy(&condition_);
  pthread_mutex_destroy(&mutex_);
}

void PlanSyncChannel::SetCurrentPhase(const PhaseId phaseId) {
  pthread_mutex_lock(&mutex_);

  currentPhase_ = phaseId;

  if (!IsGreenPhase(currentPhase_)) {
    pendingEmergencyPlan_ = PlanData{};
    hasPendingEmergency_ = false;
  }

  pthread_mutex_unlock(&mutex_);

}

bool PlanSyncChannel::PublishPlan(const PlanData& plan) {
  pthread_mutex_lock(&mutex_);

  const bool isEmergency = IsEmergencyPlan(plan);

  if (isEmergency) {
    /*
     * Emergency đến trong RED, YELLOW hoặc ALL_RED
     * phải bị loại.
     */
    if (!IsGreenPhase(currentPhase_) || shutdownRequested_) {
      pthread_mutex_unlock(&mutex_);
      return false;
    }

    /*
     * Chỉ giữ emergency plan mới nhất.
     */
    pendingEmergencyPlan_ = plan;
    hasPendingEmergency_ = true;

    /*
     * FSM chỉ wait condition variable trong GREEN.
     */
    pthread_cond_signal(&condition_);

    pthread_mutex_unlock(&mutex_);
    return true;
  }

  /*
   * Normal plan có thể đến ở bất kỳ phase nào.
   * Plan mới nhất ghi đè plan cũ chưa được áp dụng.
   */
  pendingNormalPlan_ = plan;
  hasPendingNormal_ = true;

  pthread_mutex_unlock(&mutex_);
  return true;
}

PlanSyncChannel::WaitResult PlanSyncChannel::WaitForEmergencyUntil(
    const timespec& absoluteDeadline, PlanData& outPlan) {
  pthread_mutex_lock(&mutex_);

  /*
   * Dùng vòng lặp để xử lý spurious wake-up.
   * Deadline vẫn giữ nguyên nên không làm kéo dài tick.
   */
  while (!hasPendingEmergency_ && !shutdownRequested_) {
    const int waitResult =
        pthread_cond_timedwait(&condition_, &mutex_, &absoluteDeadline);

    if (waitResult == ETIMEDOUT) {
      pthread_mutex_unlock(&mutex_);
      return WaitResult::TIMEOUT;
    }

    if (waitResult != 0) {
      pthread_mutex_unlock(&mutex_);
      return WaitResult::ERROR;
    }
  }

  if (shutdownRequested_) {
    pthread_mutex_unlock(&mutex_);
    return WaitResult::ERROR;
  }

  outPlan = pendingEmergencyPlan_;

  pendingEmergencyPlan_ = PlanData{};
  hasPendingEmergency_ = false;

  pthread_mutex_unlock(&mutex_);

  return WaitResult::EMERGENCY_AVAILABLE;
}

bool PlanSyncChannel::ConsumePendingPlan(PlanData& outPlan) {
  pthread_mutex_lock(&mutex_);

  if (!hasPendingNormal_) {
    pthread_mutex_unlock(&mutex_);
    return false;
  }

  outPlan = pendingNormalPlan_;

  pendingNormalPlan_ = PlanData{};
  hasPendingNormal_ = false;

  pthread_mutex_unlock(&mutex_);

  return true;
}

void PlanSyncChannel::RequestShutdown() {
  pthread_mutex_lock(&mutex_);

  shutdownRequested_ = true;

  /*
   * Đánh thức FSM nếu nó đang wait trong GREEN.
   */
  pthread_cond_broadcast(&condition_);

  pthread_mutex_unlock(&mutex_);
}

bool PlanSyncChannel::IsGreenPhase(const PhaseId phaseId) {
  return phaseId == PhaseId::NS_GREEN || phaseId == PhaseId::EW_GREEN;
}

bool PlanSyncChannel::IsEmergencyPlan(const PlanData& plan) {
  return plan.isEmergencyNS || plan.isEmergencyEW;
}


