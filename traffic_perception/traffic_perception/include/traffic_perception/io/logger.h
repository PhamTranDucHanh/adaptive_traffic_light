#ifndef TRAFFIC_PERCEPTION_IO_LOGGER_H_
#define TRAFFIC_PERCEPTION_IO_LOGGER_H_

#include <vector>
#include <iostream>
#include "traffic_perception/core/types.h"

namespace traffic_perception {

class Logger {
public:
    void log(const TrafficSnapshot& snapshot);
    void dump(std::ostream& out = std::cout);
    void clear();
private:
    std::vector<TrafficSnapshot> snapshots_;
};

}  // namespace traffic_perception

#endif  // TRAFFIC_PERCEPTION_IO_LOGGER_H_
