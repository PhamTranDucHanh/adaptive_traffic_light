#include "traffic_perception/io/logger.h"
#include <iostream>

namespace traffic_perception {

void Logger::log(const Timeline& timeline) {
    buffer_[head_] = timeline;
    head_ = (head_ + 1) % CAPACITY;
    if (size_ < CAPACITY) {
        size_++;
    } else {
        tail_ = (tail_ + 1) % CAPACITY;
    }
}

void Logger::dump(std::ostream& out) {
    out << "==================== TIMELINE DUMP ====================" << std::endl;
    std::size_t idx = tail_;
    for (std::size_t i = 0; i < size_; ++i) {
        const auto& tl = buffer_[idx];
        out << "  Timeline (Frame ID: " << tl.frameId << "):" << std::endl;
        if (!tl.entries.empty()) {
            auto start = tl.entries.front().timestamp;
            for (const auto& entry : tl.entries) {
                auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(entry.timestamp - start).count();
                out << "    [" << to_string(entry.stage) << "] elapsed: " << elapsed << " us" << std::endl;
            }
        }
        out << "--------------------------------------------------" << std::endl;
        idx = (idx + 1) % CAPACITY;
    }
    out << "=======================================================" << std::endl;
    clear();
}

void Logger::clear() {
    head_ = 0;
    tail_ = 0;
    size_ = 0;
}

}  // namespace traffic_perception
