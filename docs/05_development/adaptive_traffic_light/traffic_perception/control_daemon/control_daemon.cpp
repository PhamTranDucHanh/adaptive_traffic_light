#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include <unistd.h>

#include <score/mw/lifecycle/control_client.h>
#include <score/mw/lifecycle/report_running.h>

#include "control.h"
#include "ipc_dropin/socket.hpp"

namespace {

std::atomic<bool> exitRequested{false};

void signalHandler(int signal) {
  (void)signal;
  exitRequested.store(true);
}

bool sendActivationResponse(const std::string& responseSocketPath,
                            const bool success,
                            const std::string& message) noexcept {
  RunTargetResponse response{};
  response.success = success;
  std::strncpy(response.message, message.c_str(),
               sizeof(response.message) - 1U);

  ipc_dropin::Socket<sizeof(RunTargetResponse), kResponseSocketCapacity>
      responseSocket{};
  if (responseSocket.connect(responseSocketPath.c_str()) !=
          ipc_dropin::ReturnCode::kOk ||
      responseSocket.trySend(response) != ipc_dropin::ReturnCode::kOk) {
    std::cerr << "[PERCEPTION_CONTROL_DAEMON][RESPONSE][ERROR] could not send "
                 "result socket="
              << responseSocketPath << '\n';
    return false;
  }
  return true;
}

bool processExists(const pid_t pid) noexcept {
  if (pid <= 1) {
    return false;
  }

  errno = 0;
  return kill(pid, 0) == 0 || errno == EPERM;
}

}  // namespace

int main() {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;
  std::signal(SIGINT, signalHandler);
  std::signal(SIGTERM, signalHandler);

  score::mw::lifecycle::report_running();
  std::cout << "[PERCEPTION_CONTROL_DAEMON][LIFECYCLE] reported Running to "
               "Launch Manager\n";

  ipc_dropin::Socket<sizeof(RunTargetRequest), kControlSocketCapacity>
      controlSocket{};
  if (controlSocket.create(kControlSocketPath, 0600) !=
      ipc_dropin::ReturnCode::kOk) {
    std::cerr << "[PERCEPTION_CONTROL_DAEMON][INIT][ERROR] could not create "
                 "socket="
              << kControlSocketPath << '\n';
    return EXIT_FAILURE;
  }

  score::mw::lifecycle::ControlClient controlClient;
  score::safecpp::Scope<> callbackScope{};

  std::cout << "[PERCEPTION_CONTROL_DAEMON][READY] pid=" << getpid()
            << " socket=" << kControlSocketPath << '\n';
  while (!exitRequested.load()) {
    RunTargetRequest request{};
    if (controlSocket.tryReceive(request) == ipc_dropin::ReturnCode::kOk) {
      const std::string runTargetName{request.runTargetName};
      const std::string responseSocketPath{request.responseSocketPath};
      if (runTargetName == kStopCommand) {
        const pid_t launchManagerPid = getppid();
        if (!processExists(launchManagerPid)) {
          const std::string errorMessage{
              "Launch Manager parent process is not available"};
          std::cerr << "[PERCEPTION_CONTROL_DAEMON][STOP][ERROR] "
                    << errorMessage << " pid=" << launchManagerPid << '\n';
          (void)sendActivationResponse(responseSocketPath, false,
                                       errorMessage);
          continue;
        }

        std::cout << "[PERCEPTION_CONTROL_DAEMON][REQUEST] stop "
                     "launch_manager_pid="
                  << launchManagerPid << '\n';

        const std::string responseMessage =
            std::string{kShutdownPidPrefix} +
            std::to_string(static_cast<long long>(launchManagerPid));
        (void)sendActivationResponse(responseSocketPath, true,
                                     responseMessage);

        std::cout << "[PERCEPTION_CONTROL_DAEMON][STOP] requesting Launch "
                     "Manager shutdown signal=SIGTERM pid="
                  << launchManagerPid << '\n';
        if (kill(launchManagerPid, SIGTERM) != 0) {
          std::cerr << "[PERCEPTION_CONTROL_DAEMON][STOP][ERROR] "
                       "kill(SIGTERM) failed: "
                    << std::strerror(errno) << '\n';
        }
        continue;
      }

      std::cout << "[PERCEPTION_CONTROL_DAEMON][REQUEST] activate run_target="
                << runTargetName << '\n';

      controlClient.ActivateRunTarget(runTargetName).Then(
          {callbackScope,
           [runTargetName, responseSocketPath](auto& result) noexcept {
             if (result) {
               std::cout << "[PERCEPTION_CONTROL_DAEMON][SUCCESS] active "
                            "run_target="
                         << runTargetName << '\n';
               (void)sendActivationResponse(responseSocketPath, true, "");
             } else {
               const std::string errorMessage{result.error().Message()};
               std::cerr << "[PERCEPTION_CONTROL_DAEMON][ERROR] activation "
                            "failed run_target="
                         << runTargetName << " reason=" << errorMessage
                         << '\n';
               (void)sendActivationResponse(responseSocketPath, false,
                                            errorMessage);
             }
           }});
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{100});
  }

  std::cout << "[PERCEPTION_CONTROL_DAEMON][STOP] shutdown complete\n";
  return EXIT_SUCCESS;
}
