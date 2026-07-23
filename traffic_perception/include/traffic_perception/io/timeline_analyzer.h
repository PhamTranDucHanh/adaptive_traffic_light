#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <iostream>

namespace traffic_perception {

struct Timeline {
    uint32_t frameId;
    int64_t capture = -1;
    int64_t inferenceBegin = -1;
    int64_t inferenceEnd = -1;
    int64_t analyzerBegin = -1;
    int64_t analyzerEnd = -1;
    int64_t publish = -1;
};

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

class TimelineAnalyzer {
public:
    // Load a log file. Returns false if the file cannot be opened.
    bool load(const std::string& logPath);

    // Perform analysis on the loaded data.
    void analyze();

    // Print report to the provided output stream (default std::cout).
    void printReport(std::ostream& out = std::cout) const;

private:
    // Helper methods.
    void parseTimelines();
    Statistics computeStats(const std::vector<int64_t>& samples) const;

    // Raw data storage.
    std::vector<Timeline> timelines_; // only fully valid frames are kept
    std::string logPath_;

    // Statistics containers.
    std::vector<int64_t> captureSamples_;
    std::vector<int64_t> inferenceBeginSamples_;
    std::vector<int64_t> inferenceEndSamples_;
    std::vector<int64_t> analyzerBeginSamples_;
    std::vector<int64_t> analyzerEndSamples_;
    std::vector<int64_t> publishSamples_;

    // Transition latency vectors.
    std::vector<int64_t> capToInfBegin_;
    std::vector<int64_t> infBeginToEnd_;
    std::vector<int64_t> infEndToAnaBegin_;
    std::vector<int64_t> anaBeginToEnd_;
    std::vector<int64_t> anaEndToPub_;
    std::vector<int64_t> capToPub_;
};

} // namespace traffic_perception
