#ifndef APPLICATION_LOGGER_H
#define APPLICATION_LOGGER_H

#include "score/mw/log/logger.h"

namespace traffic_timing_decision {

// Function-local initialization avoids cross-translation-unit initialization
// ordering. Each logger maps one existing log category to a DLT context.
inline score::mw::log::Logger& applicationLogger() noexcept {
  static score::mw::log::Logger& logger =
      score::mw::log::CreateLogger("APP", "Application Lifecycle");
  return logger;
}

inline score::mw::log::Logger& ipcLogger() noexcept {
  static score::mw::log::Logger& logger =
      score::mw::log::CreateLogger("IPC", "POSIX Message Queue IPC");
  return logger;
}

inline score::mw::log::Logger& decisionLogger() noexcept {
  static score::mw::log::Logger& logger =
      score::mw::log::CreateLogger("DENG", "Decision Engine");
  return logger;
}

inline score::mw::log::Logger& healthLogger() noexcept {
  static score::mw::log::Logger& logger =
      score::mw::log::CreateLogger("HLTH", "Health Supervision");
  return logger;
}

}  // namespace traffic_timing_decision

#endif  // APPLICATION_LOGGER_H
