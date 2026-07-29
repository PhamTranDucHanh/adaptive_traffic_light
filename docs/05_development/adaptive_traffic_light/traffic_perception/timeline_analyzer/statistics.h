#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>

namespace timeline_analyzer {

struct Statistics {
    int64_t min = 0;
    int64_t max = 0;
    double mean = 0.0;
    int64_t median = 0;
    int64_t p90 = 0;
    int64_t p95 = 0;
    int64_t p99 = 0;
    size_t sampleCount = 0;
};

Statistics ComputeStatistics(const std::vector<int64_t>& samples);

} // namespace timeline_analyzer
