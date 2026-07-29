#ifndef PERCEPTION_DEMO_LOGGER_H
#define PERCEPTION_DEMO_LOGGER_H

#include "score/mw/log/logger.h"

namespace perception_demo {

inline score::mw::log::Logger& applicationLogger() noexcept {
  static auto& logger =
      score::mw::log::CreateLogger("PERC", "Traffic Perception Demo");
  return logger;
}

}  // namespace perception_demo

#endif  // PERCEPTION_DEMO_LOGGER_H
