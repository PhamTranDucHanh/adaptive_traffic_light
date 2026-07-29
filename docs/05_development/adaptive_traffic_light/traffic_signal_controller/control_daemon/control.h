#ifndef TRAFFIC_SIGNAL_CONTROLLER_CONTROL_DAEMON_CONTROL_H
#define TRAFFIC_SIGNAL_CONTROLLER_CONTROL_DAEMON_CONTROL_H

#include <cstddef>

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

// Use a module-specific IPC name so this standalone workspace cannot connect
// accidentally to another module's control daemon.
inline constexpr char kControlSocketPath[] = "/signal_controller_control";

// Stop is a control-plane command. The daemon handles it by signalling its
// parent Launch Manager, which performs the official managed shutdown path.
inline constexpr char kStopCommand[] = "Stop";
inline constexpr char kShutdownPidPrefix[] = "shutdown_pid=";
inline constexpr std::size_t kControlSocketCapacity = 32;
inline constexpr std::size_t kResponseSocketCapacity = 1;
inline constexpr int kActivationFailureExitCode = 2;

#endif  // TRAFFIC_SIGNAL_CONTROLLER_CONTROL_DAEMON_CONTROL_H
