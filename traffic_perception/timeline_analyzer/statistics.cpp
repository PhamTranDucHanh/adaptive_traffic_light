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
        const size_t n = sortedSamples.size();
        if (n == 1) {
            return sortedSamples.front();
        }

        const double rank = percentile * static_cast<double>(n - 1);
        const size_t lowIndex = static_cast<size_t>(std::floor(rank));
        const size_t highIndex = static_cast<size_t>(std::ceil(rank));

        if (lowIndex == highIndex) {
            return sortedSamples[lowIndex];
        }

        const double fraction = rank - static_cast<double>(lowIndex);
        const double lowValue = static_cast<double>(sortedSamples[lowIndex]);
        const double highValue = static_cast<double>(sortedSamples[highIndex]);
        return static_cast<int64_t>(std::llround(lowValue + (highValue - lowValue) * fraction));
    };

    stats.median = getPercentile(0.50);
    stats.p90 = getPercentile(0.90);
    stats.p95 = getPercentile(0.95);
    stats.p99 = getPercentile(0.99);

    return stats;
}

} // namespace timeline_analyzer
