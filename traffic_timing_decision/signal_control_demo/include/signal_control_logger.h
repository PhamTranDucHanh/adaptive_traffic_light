#ifndef SIGNAL_CONTROL_DEMO_LOGGER_H
#define SIGNAL_CONTROL_DEMO_LOGGER_H

#include "score/mw/log/logger.h"

namespace signal_control_demo {

inline score::mw::log::Logger& applicationLogger() noexcept {
  static auto& logger =
      score::mw::log::CreateLogger("SIGC", "Traffic Signal Control Demo");
  return logger;
}

}  // namespace signal_control_demo

#endif  // SIGNAL_CONTROL_DEMO_LOGGER_H
