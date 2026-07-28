#ifndef TRAFFIC_PERCEPTION_CONTROL_DAEMON_CONTROL_H
#define TRAFFIC_PERCEPTION_CONTROL_DAEMON_CONTROL_H

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

// Module-specific IPC prevents this independent workspace from connecting to
// another module's control daemon accidentally.
inline constexpr char kControlSocketPath[] = "/traffic_perception_control";

inline constexpr char kStopCommand[] = "Stop";
inline constexpr char kShutdownPidPrefix[] = "shutdown_pid=";
inline constexpr std::size_t kControlSocketCapacity = 32;
inline constexpr std::size_t kResponseSocketCapacity = 1;
inline constexpr int kActivationFailureExitCode = 2;

#endif  // TRAFFIC_PERCEPTION_CONTROL_DAEMON_CONTROL_H
