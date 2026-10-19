#ifndef TRAFFIC_PERCEPTION_CORE_STARTUP_GATE_H_
#define TRAFFIC_PERCEPTION_CORE_STARTUP_GATE_H_

#include <pthread.h>

#include <chrono>
#include <cstddef>

namespace traffic_perception {

class StartupGate final {
 public:
  StartupGate() = default;
  ~StartupGate() {
    pthread_cond_destroy(&condition_);
    pthread_mutex_destroy(&mutex_);
  }

  StartupGate(const StartupGate&) = delete;
  StartupGate& operator=(const StartupGate&) = delete;

  bool arriveAndWait(std::chrono::steady_clock::time_point& startTime) {
    if (pthread_mutex_lock(&mutex_) != 0) {
      return false;
    }

    ++readyCount_;
    pthread_cond_broadcast(&condition_);

    while (!released_ && !cancelled_) {
      if (pthread_cond_wait(&condition_, &mutex_) != 0) {
        cancelled_ = true;
        pthread_cond_broadcast(&condition_);
        pthread_mutex_unlock(&mutex_);
        return false;
      }
    }

    if (released_) {
      startTime = startTime_;
    }
    const bool shouldStart = released_ && !cancelled_;
    pthread_mutex_unlock(&mutex_);
    return shouldStart;
  }

  bool releaseWhenReady(std::size_t expectedReadyCount,
                        std::chrono::milliseconds leadTime) {
    if (pthread_mutex_lock(&mutex_) != 0) {
      return false;
    }

    while (readyCount_ < expectedReadyCount && !cancelled_) {
      if (pthread_cond_wait(&condition_, &mutex_) != 0) {
        cancelled_ = true;
        pthread_cond_broadcast(&condition_);
        pthread_mutex_unlock(&mutex_);
        return false;
      }
    }

    if (cancelled_) {
      pthread_mutex_unlock(&mutex_);
      return false;
    }

    startTime_ = std::chrono::steady_clock::now() + leadTime;
    released_ = true;
    pthread_cond_broadcast(&condition_);
    pthread_mutex_unlock(&mutex_);
    return true;
  }

  void cancel() {
    if (pthread_mutex_lock(&mutex_) != 0) {
      return;
    }
    cancelled_ = true;
    pthread_cond_broadcast(&condition_);
    pthread_mutex_unlock(&mutex_);
  }

 private:
  pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t condition_ = PTHREAD_COND_INITIALIZER;
  std::size_t readyCount_{0};
  bool released_{false};
  bool cancelled_{false};
  std::chrono::steady_clock::time_point startTime_{};
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_CORE_STARTUP_GATE_H_
