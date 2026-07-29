#ifndef ANALYTICS_SERVICE_OUTPUT_SIMULATOR_H_
#define ANALYTICS_SERVICE_OUTPUT_SIMULATOR_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#include "traffic_signal_controller/signal_fsm_engine.h"

class OutputSimulator final {
 public:
  OutputSimulator() = default;
  ~OutputSimulator();

  OutputSimulator(const OutputSimulator&) = delete;
  OutputSimulator& operator=(const OutputSimulator&) = delete;
  OutputSimulator(OutputSimulator&&) = delete;
  OutputSimulator& operator=(OutputSimulator&&) = delete;

  void start();
  void stop() noexcept;
  void submit(const SignalDisplay& display) noexcept;

 private:
  static constexpr std::uint64_t kRemainingMask{0xFFFF'FFFFULL};
  static constexpr std::uint64_t kPhaseMask{0x3ULL};
  static constexpr std::uint64_t kSequenceMask{0x3FFF'FFFFULL};

  static constexpr std::uint32_t kPhaseShift{32U};
  static constexpr std::uint32_t kSequenceShift{34U};

  void run() noexcept;
  void publish(const SignalDisplay& display) const;
  static const char* phaseName(PhaseId phaseId) noexcept;

  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> mailbox_{0U};

  // submit() is called by one producer: the FSM worker.
  std::uint64_t nextSequence_{0U};

  std::mutex notificationMutex_;
  std::condition_variable notificationCondition_;
  std::thread worker_;
};

#endif  // ANALYTICS_SERVICE_OUTPUT_SIMULATOR_H_