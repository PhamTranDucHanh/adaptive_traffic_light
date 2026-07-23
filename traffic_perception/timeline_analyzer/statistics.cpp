#include "statistics.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace timeline_analyzer {

Statistics ComputeStatistics(const std::vector<int64_t>& samples) {
    Statistics stats;
    stats.sampleCount = samples.size();
    if (samples.empty()) {
        return stats;
    }

    std::vector<int64_t> sortedSamples = samples;
    std::sort(sortedSamples.begin(), sortedSamples.end());

    stats.min = sortedSamples.front();
    stats.max = sortedSamples.back();

    double sum = 0.0;
    for (int64_t val : sortedSamples) {
        sum += static_cast<double>(val);
    }
    stats.mean = sum / static_cast<double>(sortedSamples.size());

    auto getPercentile = [&sortedSamples](double percentile) -> int64_t {
        size_t n = sortedSamples.size();
        size_t index = static_cast<size_t>(std::floor(percentile * static_cast<double>(n - 1)));
        return sortedSamples[index];
    };

    stats.median = getPercentile(0.50);
    stats.p90 = getPercentile(0.90);
    stats.p95 = getPercentile(0.95);
    stats.p99 = getPercentile(0.99);

    return stats;
}

} // namespace timeline_analyzer
