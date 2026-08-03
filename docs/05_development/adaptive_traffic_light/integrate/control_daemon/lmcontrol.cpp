#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include "control.h"
#include "ipc_dropin/socket.hpp"

namespace {

bool parseShutdownPid(const char* const message, pid_t& pid) noexcept {
  const std::size_t prefixLength = std::strlen(kShutdownPidPrefix);
  if (std::strncmp(message, kShutdownPidPrefix, prefixLength) != 0) {
    return false;
  }

  char* end{nullptr};
  errno = 0;
  const long value = std::strtol(message + prefixLength, &end, 10);
  if (errno != 0 || end == message + prefixLength || *end != '\0' ||
      value <= 1) {
    return false;
  }

  pid = static_cast<pid_t>(value);
  return true;
}

bool waitForProcessExit(const pid_t pid,
                        const std::chrono::seconds timeout) noexcept {
  constexpr auto retryDelay = std::chrono::milliseconds{10};
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    errno = 0;
    if (kill(pid, 0) != 0) {
      if (errno == ESRCH) {
        return true;
      }
      if (errno != EPERM) {
        return false;
      }
    }
    std::this_thread::sleep_for(retryDelay);
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <RunTarget|Stop>\n";
    return EXIT_FAILURE;
  }

  char responseSocketPath[kResponseSocketPathCapacity]{};
  const int pathLength =
      std::snprintf(responseSocketPath, sizeof(responseSocketPath),
                    "/sm_control_response_%ld", static_cast<long>(getpid()));
  if (pathLength < 0 ||
      static_cast<std::size_t>(pathLength) >= sizeof(responseSocketPath)) {
    std::cerr << "[LMCONTROL][ERROR] could not create response socket name\n";
    return EXIT_FAILURE;
  }

  ipc_dropin::Socket<sizeof(RunTargetResponse), kResponseSocketCapacity>
      responseSocket{};
  // lmcontrol runs outside the Lifecycle sandbox while control_daemon runs as
  // a managed process and may have a different uid/gid. The path is unique to
  // this client PID and carries only one bounded transition response. POSIX
  // applies the process umask to shm_open(), so temporarily clear it while the
  // single-threaded client creates this cross-UID response endpoint.
  const mode_t previousUmask = umask(0);
  const auto createResult = responseSocket.create(responseSocketPath, 0666);
  (void)umask(previousUmask);
  if (createResult != ipc_dropin::ReturnCode::kOk) {
    std::cerr << "[LMCONTROL][ERROR] could not create response socket="
              << responseSocketPath << '\n';
    return EXIT_FAILURE;
  }

  ipc_dropin::Socket<sizeof(RunTargetRequest), kControlSocketCapacity>
      controlSocket{};
  if (controlSocket.connect(kControlSocketPath) !=
      ipc_dropin::ReturnCode::kOk) {
    std::cerr << "[LMCONTROL][ERROR] could not connect socket="
              << kControlSocketPath << '\n';
    return EXIT_FAILURE;
  }

  RunTargetRequest request{};
  std::strncpy(request.runTargetName, argv[1],
               sizeof(request.runTargetName) - 1U);
  std::strncpy(request.responseSocketPath, responseSocketPath,
               sizeof(request.responseSocketPath) - 1U);

  if (controlSocket.trySend(request) != ipc_dropin::ReturnCode::kOk) {
    std::cerr << "[LMCONTROL][ERROR] request could not be sent\n";
    return EXIT_FAILURE;
  }

  // First verify delivery. After an abnormal previous run, a client may
  // briefly connect to stale shared memory with no daemon consuming it.
  constexpr auto deliveryTimeout = std::chrono::milliseconds{500};
  constexpr auto retryDelay = std::chrono::milliseconds{10};
  const auto deliveryDeadline =
      std::chrono::steady_clock::now() + deliveryTimeout;

  bool delivered{false};
  while (std::chrono::steady_clock::now() < deliveryDeadline) {
    RunTargetRequest* pendingRequest{nullptr};
    const auto state = controlSocket.tryPeek(pendingRequest);
    if (state == ipc_dropin::ReturnCode::kQueueEmpty) {
      delivered = true;
      break;
    }
    if (state != ipc_dropin::ReturnCode::kOk) {
      std::cerr << "[LMCONTROL][ERROR] could not observe request state\n";
      return EXIT_FAILURE;
    }
    std::this_thread::sleep_for(retryDelay);
  }

  if (!delivered) {
    std::cerr << "[LMCONTROL][ERROR] daemon did not consume request before "
                 "timeout\n";
    return EXIT_FAILURE;
  }

  std::cout << "[LMCONTROL][DELIVERED] daemon consumed request run_target="
            << argv[1] << '\n';

  // Delivery is not activation. Wait for ControlClient::ActivateRunTarget()
  // to finish so the command's exit code reflects the real transition result.
  constexpr auto activationTimeout = std::chrono::seconds{10};
  const auto activationDeadline =
      std::chrono::steady_clock::now() + activationTimeout;
  while (std::chrono::steady_clock::now() < activationDeadline) {
    RunTargetResponse response{};
    const auto state = responseSocket.tryReceive(response);
    if (state == ipc_dropin::ReturnCode::kOk) {
      if (response.success) {
        if (std::strcmp(argv[1], kStopCommand) == 0) {
          pid_t launchManagerPid{-1};
          if (!parseShutdownPid(response.message, launchManagerPid)) {
            std::cerr << "[LMCONTROL][ERROR] invalid shutdown response\n";
            return EXIT_FAILURE;
          }

          std::cout << "[LMCONTROL][ACCEPTED] Launch Manager shutdown pid="
                    << launchManagerPid << '\n';
          constexpr auto shutdownTimeout = std::chrono::seconds{15};
          if (!waitForProcessExit(launchManagerPid, shutdownTimeout)) {
            std::cerr << "[LMCONTROL][ERROR] Launch Manager did not exit "
                         "before shutdown timeout pid="
                      << launchManagerPid << '\n';
            return EXIT_FAILURE;
          }

          std::cout << "[LMCONTROL][SUCCESS] system stopped\n";
          return EXIT_SUCCESS;
        }

        std::cout << "[LMCONTROL][SUCCESS] active run_target=" << argv[1]
                  << '\n';
        return EXIT_SUCCESS;
      }
      std::cerr << "[LMCONTROL][ERROR] activation failed run_target="
                << argv[1] << " reason=" << response.message << '\n';
      return kActivationFailureExitCode;
    }
    if (state != ipc_dropin::ReturnCode::kQueueEmpty) {
      std::cerr << "[LMCONTROL][ERROR] could not read activation response\n";
      return EXIT_FAILURE;
    }
    std::this_thread::sleep_for(retryDelay);
  }

  std::cerr << "[LMCONTROL][ERROR] activation result timed out run_target="
            << argv[1] << '\n';
  return EXIT_FAILURE;
}
