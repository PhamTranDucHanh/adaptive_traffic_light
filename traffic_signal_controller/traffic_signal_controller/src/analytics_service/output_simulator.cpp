#include "analytics_service/output_simulator.h"

#include <pthread.h>
#include <sched.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <utility>

#include "common/logging_contexts.h"
#include "score/mw/log/logger.h"

namespace {

constexpr std::uint32_t kMillisecondsPerSecond{1'000U};

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

OutputSimulator::~OutputSimulator() {
  stop();
}

void OutputSimulator::start() {
  bool expected{false};

  if (!running_.compare_exchange_strong(
          expected, true, std::memory_order_acq_rel,
          std::memory_order_acquire)) {
    return;
  }

  try {
    worker_ = std::thread{&OutputSimulator::run, this};
  } catch (...) {
    running_.store(false, std::memory_order_release);
    throw;
  }
}

void OutputSimulator::stop() noexcept {
  running_.store(false, std::memory_order_release);

  {
    // Synchronize with the worker's wait operation so shutdown cannot lose
    // its notification.
    const std::lock_guard<std::mutex> lock{notificationMutex_};
  }

  notificationCondition_.notify_one();

  if (worker_.joinable()) {
    worker_.join();
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

  {
    // The mutex closes the check/wait versus publish/notify race and prevents
    // a lost wake-up. The mailbox itself remains latest-value and atomic.
    const std::lock_guard<std::mutex> lock{notificationMutex_};
  }

  notificationCondition_.notify_one();
}

void OutputSimulator::run() noexcept {
  ConfigureCurrentThreadAsNonRealtime();

  // stdout may be connected to a pipe by Launch Manager instead of a TTY.
  // Force each insertion sequence to be flushed immediately.
  std::cout.setf(std::ios::unitbuf);

  std::uint64_t lastSequence{0U};
  std::unique_lock<std::mutex> lock{notificationMutex_};

  while (true) {
    notificationCondition_.wait(lock, [this, &lastSequence]() noexcept {
      if (!running_.load(std::memory_order_acquire)) {
        return true;
      }

      const std::uint64_t packed =
          mailbox_.load(std::memory_order_acquire);
      const std::uint64_t sequence =
          (packed >> kSequenceShift) & kSequenceMask;

      return sequence != 0U && sequence != lastSequence;
    });

    if (!running_.load(std::memory_order_acquire)) {
      break;
    }

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

    // Do not hold the notification mutex while logging or writing output.
    lock.unlock();
    publish(display);
    lock.lock();
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