#ifndef TRAFFIC_PERCEPTION_IO_LOGGER_H_
#define TRAFFIC_PERCEPTION_IO_LOGGER_H_

#include <array>

#include "traffic_perception/core/types.h"

namespace traffic_perception {

class Logger {
 public:
  void log(const Timeline& timeline);
  void dump();
  void clear();

 private:
  static constexpr std::size_t CAPACITY = 100;

  std::array<Timeline, CAPACITY> buffer_{};
  std::size_t head_{0};
  std::size_t tail_{0};
  std::size_t size_{0};
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_IO_LOGGER_H_
