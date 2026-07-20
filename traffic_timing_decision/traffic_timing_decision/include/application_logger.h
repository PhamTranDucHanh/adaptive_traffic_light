#ifndef APPLICATION_LOGGER_H
#define APPLICATION_LOGGER_H

#include "score/mw/log/logger.h"

namespace traffic_timing_decision {

// One shared logging context for the complete Timing Decision process.
// Function-local initialization avoids cross-translation-unit initialization
// ordering and the returned logger is reused for every subsequent log record.
inline score::mw::log::Logger& applicationLogger() noexcept {
  static auto& logger = score::mw::log::CreateLogger(
      "DECI", "Traffic Timing Decision");
  return logger;
}

}  // namespace traffic_timing_decision

#endif  // APPLICATION_LOGGER_H
