#ifndef ANALYTICS_SERVICE_OUTPUT_SIMULATOR_H_
#define ANALYTICS_SERVICE_OUTPUT_SIMULATOR_H_

#include <semaphore.h>

#include <atomic>
#include <cstdint>

#include "traffic_signal_controller/signal_fsm_engine.h"
#include "traffic_ipc/latest_value_queue.h"
#include "traffic_ipc/signal_state_message_v1.h"

class OutputSimulator final {
 public:
  OutputSimulator();
  ~OutputSimulator();

  OutputSimulator(const OutputSimulator&) = delete;
  OutputSimulator& operator=(const OutputSimulator&) = delete;
  OutputSimulator(OutputSimulator&&) = delete;
  OutputSimulator& operator=(OutputSimulator&&) = delete;

  // Resource lifecycle is separate from thread ownership. The application
  // creates and joins the worker that executes run().
  void start();
  void run() noexcept;
  void requestStop() noexcept;
  void stop() noexcept;
  void submit(const SignalDisplay& display) noexcept;

 private:
  // Controller limits keep a complete lamp countdown below 2^20 ms.
  static constexpr std::uint64_t kCountdownMask{0xF'FFFFULL};
  static constexpr std::uint64_t kPhaseMask{0x3ULL};
  static constexpr std::uint64_t kGroupMask{0x1ULL};
  static constexpr std::uint64_t kSequenceMask{0x1F'FFFFULL};

  static constexpr std::uint32_t kEastWestCountdownShift{20U};
  static constexpr std::uint32_t kPhaseShift{40U};
  static constexpr std::uint32_t kGroupShift{42U};
  static constexpr std::uint32_t kSequenceShift{43U};

  static_assert(static_cast<std::uint64_t>(kMaxGreenDurationMs) +
                        kMaxYellowDurationMs +
                        (2ULL * kMaxAllRedDurationMs) <=
                    kCountdownMask,
                "Packed output countdown is too small for configured phases");

  void publish(const SignalDisplay& display) noexcept;
  static const char* phaseName(PhaseId phaseId) noexcept;

  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> mailbox_{0U};
  std::atomic<bool> notificationPending_{false};
  std::atomic<std::uint64_t> notificationFailureCount_{0U};
  bool started_{false};

  traffic_ipc::LatestValuePublisher<traffic_ipc::SignalStateMessageV1>
      signalStatePublisher_{traffic_ipc::kSignalStateQueueName,
                            traffic_ipc::kSignalStateLockName};
  std::uint64_t signalStateSequence_{0U};
  std::uint64_t signalStatePublishFailureCount_{0U};
  bool signalStatePublisherOpen_{false};

  // submit() is called by one producer: the FSM worker.
  std::uint64_t nextSequence_{0U};

  sem_t notificationSemaphore_{};
};

#endif  // ANALYTICS_SERVICE_OUTPUT_SIMULATOR_H_
