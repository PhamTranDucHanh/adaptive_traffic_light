#ifndef PLAN_SYNC_CHANNEL_H
#define PLAN_SYNC_CHANNEL_H

#include <common/config.h>
#include <pthread.h>

// Kênh handoff thread-safe giữa PlanReceiver (writer) và SignalFSMEngine (reader).
class PlanSyncChannel {
 public:
  PlanSyncChannel();
  ~PlanSyncChannel();

  void PublishPlan(const PlanData& plan);

  // Chờ tới deadline; trả về true nếu có plan mới xuất hiện trong lúc chờ.
  // Không "consume" — chỉ báo hiệu có plan đang chờ, dữ liệu vẫn giữ nguyên bên trong.
  bool WaitForPlan(const struct timespec& deadline);

  // Lấy plan đang chờ (nếu có) và xóa cờ hasPending_.
  // Gọi ở nhánh ALL_RED khi thật sự sẵn sàng áp dụng.
  bool ConsumePendingPlan(PlanData& out_plan);

 private:
  pthread_mutex_t mutex_;
  pthread_cond_t cond_;
  PlanData pendingPlan_;
  bool hasPending_;
};

#endif