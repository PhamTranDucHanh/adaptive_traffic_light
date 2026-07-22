#include "traffic_perception/io/logger.h"
#include <iostream>

namespace traffic_perception {

void Logger::log(const TrafficSnapshot& snapshot) {
    snapshots_.push_back(snapshot);
}

void Logger::dump(std::ostream& out) {
    out << "==================================================" << std::endl;
    for (const auto& s : snapshots_) {
        out << "FrameId: " << s.frameId << std::endl;
        out << "TimestampUs: " << s.timestampUs << std::endl;
        out << "Lane North - Count: " << s.vehicleCountNorth << ", Occ: " << s.occupancyNorth << ", Queue: " << s.queueLengthNorth << std::endl;
        out << "Lane South - Count: " << s.vehicleCountSouth << ", Occ: " << s.occupancySouth << ", Queue: " << s.queueLengthSouth << std::endl;
        out << "Lane East - Count: " << s.vehicleCountEast << ", Occ: " << s.occupancyEast << ", Queue: " << s.queueLengthEast << std::endl;
        out << "Lane West - Count: " << s.vehicleCountWest << ", Occ: " << s.occupancyWest << ", Queue: " << s.queueLengthWest << std::endl;
        out << "--------------------------------------------------" << std::endl;
    }
    out << "==================================================" << std::endl;
    clear();
}

void Logger::clear() {
    snapshots_.clear();
}

}  // namespace traffic_perception
