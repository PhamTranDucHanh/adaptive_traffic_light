#include "common/plan_sync_channel.h"

PlanSyncChannel::PlanSyncChannel() : hasPending_(false) {
    pthread_mutex_init(&mutex_, nullptr);
    pthread_cond_init(&cond_, nullptr);
}

PlanSyncChannel::~PlanSyncChannel() {
    pthread_mutex_destroy(&mutex_);
    pthread_cond_destroy(&cond_);
}

void PlanSyncChannel::PublishPlan(const PlanData& plan) {
    pthread_mutex_lock(&mutex_);
    pendingPlan_ = plan;
    hasPending_ = true;
    pthread_cond_signal(&cond_);
    pthread_mutex_unlock(&mutex_);
}

bool PlanSyncChannel::WaitForPlan(const struct timespec& deadline) {
    pthread_mutex_lock(&mutex_);
    bool already = hasPending_;
    if (!already) {
        pthread_cond_timedwait(&cond_, &mutex_, &deadline);
    }
    bool result = hasPending_;
    pthread_mutex_unlock(&mutex_);
    return result;
}

bool PlanSyncChannel::ConsumePendingPlan(PlanData& out_plan) {
    pthread_mutex_lock(&mutex_);
    bool got = hasPending_;
    if (got) {
        out_plan = pendingPlan_;
        hasPending_ = false;
    }
    pthread_mutex_unlock(&mutex_);
    return got;
}