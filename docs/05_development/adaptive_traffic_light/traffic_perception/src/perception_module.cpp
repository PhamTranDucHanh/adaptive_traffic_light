#include "traffic_perception/perception_module.h"

#include <pthread.h>
#include <sched.h>

#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <string>

#include "score/mw/log/logging.h"
#include "traffic_perception/inference/rt_detrv2_backend.h"
#include "traffic_perception/inference/yolov8_backend.h"
#include "traffic_perception/inference/yolov8_oiv7_backend.h"

namespace {

constexpr std::array<std::int32_t, 2> kAllowedPerceptionCpus{0, 2};

struct BackendBootstrapContext {
  const AppConfig* config{nullptr};
  std::unique_ptr<traffic_perception::IModelBackend> backend{};
  std::exception_ptr exception{};
};

void* BackendBootstrapThreadEntry(void* arg) {
  auto* ctx = static_cast<BackendBootstrapContext*>(arg);

  try {
    if (ctx->config->modelBackend == "yolov8") {
      ctx->backend = std::make_unique<traffic_perception::YOLOv8Backend>(
          ctx->config->modelPath, ctx->config->emergencyClass,
          ctx->config->Threading.Pipeline.Core,
          ctx->config->Threading.Pipeline.Priority);
    } else if (ctx->config->modelBackend == "yolov8_oiv7") {
      ctx->backend = std::make_unique<traffic_perception::YoloV8OIV7Backend>(
          ctx->config->modelPath, ctx->config->emergencyClass,
          ctx->config->Threading.Pipeline.Core,
          ctx->config->Threading.Pipeline.Priority);
    } else if (ctx->config->modelBackend == "rt_detrv2") {
      ctx->backend = std::make_unique<traffic_perception::RtDetrv2Backend>(
          ctx->config->modelPath, ctx->config->emergencyClass,
          ctx->config->Threading.Pipeline.Core,
          ctx->config->Threading.Pipeline.Priority);
    }
  } catch (...) {
    // Exceptions must not cross the pthread C entry-point boundary. The join
    // below synchronizes this state before it is rethrown by the caller.
    ctx->exception = std::current_exception();
  }

  return nullptr;
}

bool CreateBackendOnOtherThread(
    const AppConfig& config,
    std::unique_ptr<traffic_perception::IModelBackend>& backend) {
  pthread_attr_t attr;
  int ret = pthread_attr_init(&attr);
  if (ret != 0) {
    return false;
  }

  ret = pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
  if (ret == 0) {
    ret = pthread_attr_setschedpolicy(&attr, SCHED_OTHER);
  }
  sched_param schedulingParameters{};
  schedulingParameters.sched_priority = 0;
  if (ret == 0) {
    ret = pthread_attr_setschedparam(&attr, &schedulingParameters);
  }

  cpu_set_t affinityMask{};
  CPU_ZERO(&affinityMask);
  // Session construction may run on any available CPU. The ORT worker threads
  // themselves receive one-CPU masks in the custom thread creation callback.
  for (const std::int32_t cpu : kAllowedPerceptionCpus) {
    CPU_SET(cpu, &affinityMask);
  }
  if (ret == 0) {
    ret = pthread_attr_setaffinity_np(&attr, sizeof(affinityMask),
                                      &affinityMask);
  }

  BackendBootstrapContext context{&config};
  pthread_t thread{};
  if (ret == 0) {
    ret = pthread_create(&thread, &attr, BackendBootstrapThreadEntry, &context);
  }
  pthread_attr_destroy(&attr);

  if (ret != 0) {
    score::mw::log::LogError()
        << "[PERCEPTION_MODULE][INIT] backend bootstrap thread failed; ret="
        << ret << "; reason=" << std::string{strerror(ret)};
    return false;
  }

  ret = pthread_join(thread, nullptr);
  if (ret != 0) {
    score::mw::log::LogError()
        << "[PERCEPTION_MODULE][INIT] backend bootstrap join failed; ret="
        << ret << "; reason=" << std::string{strerror(ret)};
    return false;
  }

  if (context.exception != nullptr) {
    std::rethrow_exception(context.exception);
  }

  backend = std::move(context.backend);
  return backend != nullptr;
}

void* StreamThreadEntry(void* arg) {
  auto* ctx = static_cast<traffic_perception::StreamThreadContext*>(arg);

  char name[16];
  snprintf(name, sizeof(name), "STRM%d", ctx->worker->getLaneId());

  pthread_setname_np(pthread_self(), name);

  ctx->worker->run(*ctx->buffer, ctx->startTime);
  return nullptr;
}

void* PipelineThreadEntry(void* arg) {
  auto* ctx = static_cast<traffic_perception::PipelineThreadContext*>(arg);

  pthread_setname_np(pthread_self(), "PIPE");

  ctx->pipeline->run(ctx->startTime);
  return nullptr;
}

}  // namespace

namespace traffic_perception {

bool PerceptionModule::initModule(const AppConfig& config) {
  score::mw::log::LogInfo() << "[PERCEPTION_MODULE][INIT]\n";

  // Newer OpenCV FFmpeg backends honor OPENCV_FFMPEG_THREADS and will limit
  // each decoder to one thread. Ubuntu's OpenCV 4.6 does not recognize this
  // setting, so it safely falls back to its existing four decoder threads.
  // Set it once before any concurrent VideoCapture open because environment
  // variables are process-global and OpenCV may cache them on first use. This
  // keeps the same binary/source compatible with both Ubuntu installations.
  (void)setenv("OPENCV_FFMPEG_THREADS", "1", 1);

  // Keep OpenCV work on the explicitly pinned caller thread. Its default
  // parallel backend otherwise creates NCPU-1 helpers which inherit the PIPE
  // thread's real-time policy and name but are allowed to run on every CPU.
  cv::setNumThreads(1);

  config_ = config;
  startTime_ = std::chrono::steady_clock::now();

  if (!pool_.init(20, config_.videoResolution.width, config_.videoResolution.height)) {
    return false;
  }

  if (config_.modelBackend != "yolov8" &&
      config_.modelBackend != "yolov8_oiv7" &&
      config_.modelBackend != "rt_detrv2") {
    score::mw::log::LogError()
        << "[PERCEPTION_MODULE][INIT] unsupported model backend: "
        << config_.modelBackend;
    return false;
  }

  // The managed process starts as SCHED_RR. Construct ONNX Runtime from an
  // explicitly SCHED_OTHER, CPU-0 bootstrap thread so its internal workers
  // inherit the intended scheduling and affinity at creation time.
  if (!CreateBackendOnOtherThread(config_, backend_)) {
    return false;
  }

  std::array<Roi, NUM_LANES> laneRois{};
  for (std::size_t i = 0; i < NUM_LANES; ++i) {
    laneRois[i] = config_.lanes[i].roi;
  }

  pipelineManager_ = std::make_unique<PipelineManager>(
      *backend_, buffer_, pool_, laneRois, config_.PipelinePeriod,
      config_.PipelinePhase);
  pipelineThreadContext_.pipeline = pipelineManager_.get();

  if (!snapshotSender_.open()) {
    score::mw::log::LogError()
        << "[PERCEPTION_MODULE][INIT] snapshot IPC initialization failed";
    return false;
  }
  snapshotSenderOpen_ = true;
  pipelineManager_->getPublisher().initSender(&snapshotSender_);

  for (std::size_t i = 0; i < NUM_LANES; ++i) {
    workers_[i].initStream(config_.lanes[i].videoSource,
                           static_cast<std::int32_t>(i), &pool_,
                           config_.CapturePeriod, config_.CapturePhase,
                           config_.Threading.StreamWorkers[i].Core,
                           config_.Threading.StreamWorkers[i].Priority);

    streamThreadContexts_[i].worker = &workers_[i];
    streamThreadContexts_[i].buffer = &buffer_;
  }

  for (std::size_t i = 0; i < NUM_LANES; ++i) {
    if (!ConfigureRealtimeThreadAttr(streamThreadAttrs_[i],
                                     config_.Threading.StreamWorkers[i])) {
      return false;
    }
  }

  if (!ConfigureRealtimeThreadAttr(pipelineThreadAttr_,
                                   config_.Threading.Pipeline)) {
    return false;
  }

  return true;
}

bool PerceptionModule::startThreads() {
  if (started_) {
    return true;
  }

  for (std::size_t i = 0; i < NUM_LANES; ++i) {
    streamThreadContexts_[i].startTime = startTime_;
  }
  pipelineThreadContext_.startTime = startTime_;

  for (std::size_t i = 0; i < NUM_LANES; ++i) {
    int ret = pthread_create(&streamThreads_[i], &streamThreadAttrs_[i],
                             StreamThreadEntry, &streamThreadContexts_[i]);

    if (ret == EPERM) {
      // Insufficient RT scheduling privileges — fall back to SCHED_OTHER
      // so the system can still run on dev machines without CAP_SYS_NICE.
      score::mw::log::LogWarn()
          << "[PerceptionModule] stream thread " << i
          << " SCHED_RR/FIFO denied (EPERM), retrying with SCHED_OTHER";
      pthread_attr_t fallbackAttr;
      pthread_attr_init(&fallbackAttr);
      ret = pthread_create(&streamThreads_[i], &fallbackAttr,
                           StreamThreadEntry, &streamThreadContexts_[i]);
      pthread_attr_destroy(&fallbackAttr);
    }

    if (ret != 0) {
      score::mw::log::LogError()
          << "Failed to create stream thread " << i << " errno=" << ret << " "
          << std::string{strerror(ret)};

      // Join any stream threads that were already started before this failure
      for (auto& worker : workers_) {
        worker.stop();
      }
      for (std::size_t j = 0; j < i; ++j) {
        pthread_join(streamThreads_[j], nullptr);
      }
      return false;
    }
  }

  int ret = pthread_create(&pipelineThread_, &pipelineThreadAttr_,
                           PipelineThreadEntry, &pipelineThreadContext_);

  if (ret == EPERM) {
    score::mw::log::LogWarn()
        << "[PerceptionModule] pipeline thread SCHED_RR/FIFO denied (EPERM),"
           " retrying with SCHED_OTHER";
    pthread_attr_t fallbackAttr;
    pthread_attr_init(&fallbackAttr);
    ret = pthread_create(&pipelineThread_, &fallbackAttr,
                         PipelineThreadEntry, &pipelineThreadContext_);
    pthread_attr_destroy(&fallbackAttr);
  }

  if (ret != 0) {
    score::mw::log::LogError() << "Failed to create pipeline thread " << ret
                               << " " << std::string{strerror(ret)};

    // All stream threads were started; stop and join them before returning
    for (auto& worker : workers_) {
      worker.stop();
    }
    for (auto& thread : streamThreads_) {
      pthread_join(thread, nullptr);
    }
    return false;
  }

  started_ = true;
  return true;
}

void PerceptionModule::stopThreads() {
  if (!started_) {
    return;
  }

  for (auto& worker : workers_) {
    worker.stop();
  }

  if (pipelineManager_ != nullptr) {
    pipelineManager_->stop();
  }

  for (auto& thread : streamThreads_) {
    pthread_join(thread, nullptr);
  }

  pthread_join(pipelineThread_, nullptr);

  for (auto& attr : streamThreadAttrs_) {
    pthread_attr_destroy(&attr);
  }
  pthread_attr_destroy(&pipelineThreadAttr_);

  started_ = false;

  if (snapshotSenderOpen_) {
    snapshotSender_.close();
    snapshotSenderOpen_ = false;
  }
}

bool PerceptionModule::ConfigureRealtimeThreadAttr(pthread_attr_t& attr,
                                                   const ThreadConfig& config) {
  pthread_attr_init(&attr);

  pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);

  int policy = SCHED_OTHER;

  if (config.Policy == "SCHED_FIFO") {
    policy = SCHED_FIFO;
  } else if (config.Policy == "SCHED_RR") {
    policy = SCHED_RR;
  }

  pthread_attr_setschedpolicy(&attr, policy);

  sched_param param{};
  param.sched_priority = config.Priority;

  pthread_attr_setschedparam(&attr, &param);

  if (config.Core >= 0) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(config.Core, &cpuset);
    int affinity_ret = pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);
    if (affinity_ret != 0) {
      score::mw::log::LogWarn()
          << "[PerceptionModule] pthread_attr_setaffinity_np failed for core "
          << config.Core << " ret=" << affinity_ret;
    }
  }

  return true;
}

IModelBackend* PerceptionModule::backend() { return backend_.get(); }

Analyzer& PerceptionModule::analyzer() { return pipelineManager_->analyzer(); }

const Analyzer& PerceptionModule::analyzer() const {
  return pipelineManager_->analyzer();
}

std::array<cv::Mat, NUM_LANES> PerceptionModule::latestPreviewFrames() const {
  std::array<cv::Mat, NUM_LANES> previews{};
  for (std::size_t i = 0; i < NUM_LANES; ++i) {
    previews[i] = workers_[i].latestPreviewFrame();
  }
  return previews;
}

}  // namespace traffic_perception
