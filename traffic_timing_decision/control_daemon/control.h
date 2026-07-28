#ifndef CONTROL_DAEMON_CONTROL_H
#define CONTROL_DAEMON_CONTROL_H

#include <cstddef>

// Keep RunTargetRequest at 1024 bytes for compatibility with an already
// running daemon from the previous protocol version.
inline constexpr std::size_t kRunTargetNameCapacity = 896;
inline constexpr std::size_t kResponseSocketPathCapacity = 128;
inline constexpr std::size_t kResponseMessageCapacity = 512;

struct RunTargetRequest {
  char runTargetName[kRunTargetNameCapacity]{};
  char responseSocketPath[kResponseSocketPathCapacity]{};
};
static_assert(sizeof(RunTargetRequest) == 1024);

struct RunTargetResponse {
  bool success{false};
  char message[kResponseMessageCapacity]{};
};

inline constexpr char kControlSocketPath[] = "/sm_control";
// Stop is a control-plane command, not a Lifecycle Run Target. The control
// daemon handles it by signalling its parent Launch Manager so that the
// official signal-driven shutdown path transitions all process groups Off.
inline constexpr char kStopCommand[] = "Stop";
inline constexpr char kShutdownPidPrefix[] = "shutdown_pid=";
inline constexpr std::size_t kControlSocketCapacity = 32;
inline constexpr std::size_t kResponseSocketCapacity = 1;
inline constexpr int kActivationFailureExitCode = 2;

#endif  // CONTROL_DAEMON_CONTROL_H
