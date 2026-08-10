#include "traffic_perception/inference/onnx_thread_pool.h"

#include <pthread.h>
#include <sched.h>

#include <cstdio>
#include <new>
#include <array>
#include <algorithm>

namespace traffic_perception {
namespace {

constexpr std::array<int, 2> kAllowedInferenceCpus{0, 2};

int LowerRealtimePriority(const int parentPriority) {
  const int minimumPriority = sched_get_priority_min(SCHED_RR);
  return std::max(minimumPriority, parentPriority - 1);
}

struct OrtPthreadHandle {
  pthread_t thread{};
  OrtThreadWorkerFn workerFn{nullptr};
  void* workerParam{nullptr};
  int cpu{-1};
  int priority{1};
};

void* OrtWorkerEntry(void* arg) {
  auto* handle = static_cast<OrtPthreadHandle*>(arg);

  char name[16]{};
  std::snprintf(name, sizeof(name), "ONNX-CPU%d", handle->cpu);
  (void)pthread_setname_np(pthread_self(), name);

  handle->workerFn(handle->workerParam);
  return nullptr;
}

OrtCustomThreadHandle CreateOrtWorker(void* creationOptions,
                                      OrtThreadWorkerFn workerFn,
                                      void* workerParam) {
  auto* config = static_cast<OrtThreadPoolConfig*>(creationOptions);
  if (config == nullptr || workerFn == nullptr) {
    return nullptr;
  }

  const std::size_t workerIndex = config->nextWorker.fetch_add(1);
  if (workerIndex >= config->workerCpus.size()) {
    std::fprintf(stderr,
                 "ONNX Runtime requested more than %zu intra-op workers\n",
                 config->workerCpus.size());
    return nullptr;
  }

  auto* handle = new (std::nothrow) OrtPthreadHandle;
  if (handle == nullptr) {
    return nullptr;
  }
  handle->workerFn = workerFn;
  handle->workerParam = workerParam;
  handle->cpu = config->workerCpus[workerIndex];
  handle->priority = config->workerPriority;

  pthread_attr_t attr;
  int ret = pthread_attr_init(&attr);
  if (ret != 0) {
    delete handle;
    return nullptr;
  }

  cpu_set_t affinityMask{};
  CPU_ZERO(&affinityMask);
  CPU_SET(handle->cpu, &affinityMask);
  ret = pthread_attr_setaffinity_np(&attr, sizeof(affinityMask), &affinityMask);

  if (ret == 0) {
    ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  }
  if (ret == 0) {
    ret = pthread_attr_setschedpolicy(&attr, SCHED_RR);
  }
  sched_param schedulingParameters{};
  schedulingParameters.sched_priority = handle->priority;
  if (ret == 0) {
    ret = pthread_attr_setschedparam(&attr, &schedulingParameters);
  }
  if (ret == 0) {
    ret = pthread_create(&handle->thread, &attr, OrtWorkerEntry, handle);
  }

  (void)pthread_attr_destroy(&attr);
  if (ret != 0) {
    std::fprintf(stderr,
                 "Failed to create ONNX Runtime RT worker on CPU %d at "
                 "priority %d; ret=%d\n",
                 handle->cpu, handle->priority, ret);
    delete handle;
    return nullptr;
  }

  return reinterpret_cast<OrtCustomThreadHandle>(handle);
}

void JoinOrtWorker(OrtCustomThreadHandle rawHandle) {
  auto* handle = const_cast<OrtPthreadHandle*>(
      reinterpret_cast<const OrtPthreadHandle*>(rawHandle));
  if (handle == nullptr) {
    return;
  }

  (void)pthread_join(handle->thread, nullptr);
  delete handle;
}

}  // namespace

OrtThreadPoolConfig::OrtThreadPoolConfig(int inferenceCallerCpu,
                                         int inferenceCallerPriority)
    : workerPriority(LowerRealtimePriority(inferenceCallerPriority)) {
  std::size_t workerIndex = 0;
  for (const int cpu : kAllowedInferenceCpus) {
    if (cpu != inferenceCallerCpu && workerIndex < workerCpus.size()) {
      workerCpus[workerIndex++] = cpu;
    }
  }
}

void ConfigureOrtThreadPool(Ort::SessionOptions& sessionOptions,
                            OrtThreadPoolConfig& threadPoolConfig) {
  threadPoolConfig.nextWorker.store(0);
  sessionOptions.SetIntraOpNumThreads(static_cast<int>(kOrtIntraOpThreadCount));
  sessionOptions.SetInterOpNumThreads(1);
  sessionOptions.SetCustomCreateThreadFn(CreateOrtWorker);
  sessionOptions.SetCustomThreadCreationOptions(&threadPoolConfig);
  sessionOptions.SetCustomJoinThreadFn(JoinOrtWorker);
}

}  // namespace traffic_perception
