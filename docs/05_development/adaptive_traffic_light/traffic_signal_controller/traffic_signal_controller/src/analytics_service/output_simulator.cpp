#include "analytics_service/output_simulator.h"

#include <cerrno>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "common/logging_contexts.h"
#include "score/mw/log/logger.h"

namespace {

constexpr std::uint32_t kMillisecondsPerSecond{1'000U};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "Output mailbox must be lock-free");
static_assert(std::atomic<bool>::is_always_lock_free,
              "Output notification state must be lock-free");

score::mw::log::Logger& Logger() {
  static auto& logger = score::mw::log::CreateLogger(
      ctrl::logging::kCtxOut, "Output Simulator");
  return logger;
}

void ConfigureCurrentThreadAsNonRealtime() noexcept {
  sched_param parameters{};
  parameters.sched_priority = 0;

  const int result =
      pthread_setschedparam(pthread_self(), SCHED_OTHER, &parameters);

  if (result == 0) {
    Logger().LogInfo() << "event=THREAD_SCHEDULING_CONFIGURED"
                       << ", thread=output_simulator"
                       << ", policy=SCHED_OTHER"
                       << ", priority=0";
    return;
  }

  Logger().LogWarn() << "event=THREAD_SCHEDULING_FAILED"
                     << ", thread=output_simulator"
                     << ", policy=SCHED_OTHER"
                     << ", priority=0"
                     << ", error=" << result
                     << ", reason=" << std::strerror(result);
}

}  // namespace

OutputSimulator::OutputSimulator() {
  if (sem_init(&notificationSemaphore_, 0, 0U) != 0) {
    throw std::runtime_error(
        "Failed to initialize OutputSimulator notification semaphore");
  }
}

OutputSimulator::~OutputSimulator() {
  stop();
  (void)sem_destroy(&notificationSemaphore_);
}

void OutputSimulator::start() {
  bool expected{false};

  if (!running_.compare_exchange_strong(
          expected, true, std::memory_order_acq_rel,
          std::memory_order_acquire)) {
    return;
  }

  notificationPending_.store(false, std::memory_order_release);
  for (;;) {
    if (sem_trywait(&notificationSemaphore_) == 0) {
      continue;
    }
    if (errno == EINTR) {
      continue;
    }
    break;
  }

  try {
    worker_ = std::thread{&OutputSimulator::run, this};
  } catch (...) {
    running_.store(false, std::memory_order_release);
    throw;
  }
}

void OutputSimulator::stop() noexcept {
  const bool wasRunning = running_.exchange(false, std::memory_order_acq_rel);
  if (!wasRunning && !worker_.joinable()) {
    return;
  }

  (void)sem_post(&notificationSemaphore_);

  if (worker_.joinable()) {
    worker_.join();
  }

  const std::uint64_t notificationFailures =
      notificationFailureCount_.exchange(0U, std::memory_order_acq_rel);
  if (notificationFailures > 0U) {
    Logger().LogWarn() << "event=OUTPUT_NOTIFICATION_FAILED"
                       << ", count=" << notificationFailures;
  }
}

void OutputSimulator::submit(const SignalDisplay& display) noexcept {
  std::uint64_t sequence = (nextSequence_ + 1U) & kSequenceMask;

  // Sequence zero represents an empty mailbox.
  if (sequence == 0U) {
    sequence = 1U;
  }

  nextSequence_ = sequence;

  const std::uint64_t packed =
      (sequence << kSequenceShift) |
      ((static_cast<std::uint64_t>(display.phaseId) & kPhaseMask)
       << kPhaseShift) |
      (static_cast<std::uint64_t>(display.remainingTimeMs) & kRemainingMask);

  mailbox_.store(packed, std::memory_order_release);

  if (!notificationPending_.exchange(true, std::memory_order_acq_rel) &&
      sem_post(&notificationSemaphore_) != 0) {
    notificationPending_.store(false, std::memory_order_release);
    notificationFailureCount_.fetch_add(1U, std::memory_order_relaxed);
  }
}

void OutputSimulator::run() noexcept {

  const int nameResult =
      pthread_setname_np(pthread_self(), "tsc_output");

  if (nameResult != 0) {
    Logger().LogWarn() << "event=THREAD_NAME_FAILED"
                       << ", thread=tsc_output"
                       << ", error=" << nameResult
                       << ", reason=" << std::strerror(nameResult);
  }

  ConfigureCurrentThreadAsNonRealtime();

  std::cout.setf(std::ios::unitbuf);

  std::uint64_t lastSequence{0U};

  while (running_.load(std::memory_order_acquire)) {
    int waitResult{};
    do {
      waitResult = sem_wait(&notificationSemaphore_);
    } while (waitResult != 0 && errno == EINTR);

    if (waitResult != 0) {
      const int errorNumber = errno;
      Logger().LogWarn()
          << "event=OUTPUT_NOTIFICATION_WAIT_FAILED"
          << ", error=" << errorNumber
          << ", reason=" << std::string_view{std::strerror(errorNumber)};
      running_.store(false, std::memory_order_release);
      break;
    }

    if (!running_.load(std::memory_order_acquire)) {
      break;
    }

    // Clear before loading the mailbox. A concurrent submit either becomes
    // visible in this load or posts the next semaphore notification.
    notificationPending_.store(false, std::memory_order_release);

    const std::uint64_t packed =
        mailbox_.load(std::memory_order_acquire);
    const std::uint64_t sequence =
        (packed >> kSequenceShift) & kSequenceMask;

    if (sequence == 0U || sequence == lastSequence) {
      continue;
    }

    SignalDisplay display{};
    display.phaseId = static_cast<PhaseId>(
        (packed >> kPhaseShift) & kPhaseMask);
    display.remainingTimeMs = static_cast<std::uint32_t>(
        packed & kRemainingMask);

    lastSequence = sequence;
    publish(display);
  }
}

void OutputSimulator::publish(const SignalDisplay& display) const {
  std::cout << "Phase: " << phaseName(display.phaseId)
            << ", Remaining: "
            << (display.remainingTimeMs / kMillisecondsPerSecond)
            << " s\n";
}

const char* OutputSimulator::phaseName(const PhaseId phaseId) noexcept {
  switch (phaseId) {
    case PhaseId::NS_GREEN:
      return "NS_GREEN";

    case PhaseId::EW_GREEN:
      return "EW_GREEN";

    case PhaseId::YELLOW:
      return "YELLOW";

    case PhaseId::ALL_RED:
      return "ALL_RED";
  }

  return "UNKNOWN";
}
