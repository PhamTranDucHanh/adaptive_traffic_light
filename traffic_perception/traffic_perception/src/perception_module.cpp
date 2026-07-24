#include "traffic_perception/perception_module.h"

#include <pthread.h>
#include <sched.h>

#include <memory>

#include "score/mw/log/logging.h"
#include "traffic_perception/inference/yolov8_backend.h"
#include "traffic_perception/inference/yolov8_oiv7_backend.h"

namespace {

void* StreamThreadEntry(void* arg) {
  auto* ctx =
      static_cast<traffic_perception::StreamThreadContext*>(
          arg);

  ctx->worker->run(*ctx->buffer);
  return nullptr;
}

void* PipelineThreadEntry(void* arg) {
  auto* pipeline =
      static_cast<traffic_perception::PipelineManager*>(arg);

  pipeline->run();
  return nullptr;
}

}  // namespace

namespace traffic_perception {

bool PerceptionModule::initModule(const AppConfig& config) {
  score::mw::log::LogInfo()
      << "[PERCEPTION_MODULE][INIT]\n";

  config_ = config;

  if (!pool_.init(20)) {
    return false;
  }

  backend_ =
      std::make_unique<YoloV8OIV7Backend>(config_.modelPath);

  std::array<Roi, NUM_LANES> laneRois{};
  for (std::size_t i = 0; i < NUM_LANES; ++i) {
    laneRois[i] = config_.lanes[i].roi;
  }

pipelineManager_ = std::make_unique<PipelineManager>(
    *backend_,
    buffer_,
    pool_,
    laneRois,
    config_.PipelinePeriod,
    config_.PipelinePhase);

  for (std::size_t i = 0; i < NUM_LANES; ++i) {
    workers_[i].initStream(
        config_.lanes[i].videoSource,
        static_cast<std::int32_t>(i),
        &pool_,
        config_.CapturePeriod,
        config_.CapturePhase);

    streamThreadContexts_[i].worker = &workers_[i];
    streamThreadContexts_[i].buffer = &buffer_;
  }

  for (std::size_t i = 0; i < NUM_LANES; ++i) {
    if (!ConfigureRealtimeThreadAttr(
            streamThreadAttrs_[i],
            config_.Threading.Stream)) {
      return false;
    }
  }

  if (!ConfigureRealtimeThreadAttr(
          pipelineThreadAttr_,
          config_.Threading.Pipeline)) {
    return false;
  }

  snapshotSenderOpen_ = true;
  return true;
}

bool PerceptionModule::startThreads() {
  if (started_) {
    return true;
  }

  for (std::size_t i = 0; i < NUM_LANES; ++i) {
    int ret = pthread_create(
        &streamThreads_[i],
        &streamThreadAttrs_[i],
        StreamThreadEntry,
        &streamThreadContexts_[i]);

    if (ret != 0) {
        score::mw::log::LogError()
            << "Failed to create stream thread "
            << i
            << " errno="
            << ret
            << " "
            << strerror(ret);

        return false;
    }
  }

    int ret = pthread_create(
        &pipelineThread_,
        &pipelineThreadAttr_,
        PipelineThreadEntry,
        pipelineManager_.get());

    if (ret != 0) {
        score::mw::log::LogError()
            << "Failed to create pipeline thread "
            << ret
            << " "
            << strerror(ret);

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

  started_ = false;

  if (snapshotSenderOpen_) {
    snapshotSender_.close();
    snapshotSenderOpen_ = false;
  }
}

bool PerceptionModule::ConfigureRealtimeThreadAttr(
    pthread_attr_t& attr,
    const ThreadConfig& config) {
  pthread_attr_init(&attr);

  pthread_attr_setinheritsched(
      &attr,
      PTHREAD_EXPLICIT_SCHED);

  int policy = SCHED_OTHER;

  if (config.Policy == "SCHED_FIFO") {
    policy = SCHED_FIFO;
  } else if (config.Policy == "SCHED_RR") {
    policy = SCHED_RR;
  }

  pthread_attr_setschedpolicy(
      &attr,
      policy);

  sched_param param{};
  param.sched_priority = config.Priority;

  pthread_attr_setschedparam(
      &attr,
      &param);

  return true;
}

IModelBackend* PerceptionModule::backend() {
    return backend_.get();
}

Analyzer& PerceptionModule::analyzer() {
    return pipelineManager_->analyzer();
}

const Analyzer& PerceptionModule::analyzer() const {
    return pipelineManager_->analyzer();
}

}  // namespace traffic_perception
