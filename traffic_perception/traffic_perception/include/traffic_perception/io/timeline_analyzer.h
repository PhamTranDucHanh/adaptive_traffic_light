#include <cstdint>
#include <string>
#include <vector>
#include <iostream>

namespace traffic_perception {

class TimelineAnalyzer {
public:
    explicit TimelineAnalyzer(const std::string& logFile) : logFile_(logFile) {}

    // Load log file contents into memory.
    bool load();
    // Parse and compute statistics.
    bool analyze();
    // Perform per‑lane analysis and print a concise report for the given laneId (0‑based).
    void analyzeLane(int laneId) const;
    void printReport(std::ostream& out) const;

    struct Statistics {
        int64_t min = 0;
        int64_t max = 0;
        double mean = 0.0;
        int64_t median = 0;
        int64_t p90 = 0;
        int64_t p95 = 0;
        int64_t p99 = 0;
        double stddev = 0.0;
        size_t sampleCount = 0;
    };

private:
    struct Timeline {
        uint32_t frameId = 0;
        int laneId = -1;
        int64_t capture = 0;
        int64_t inferenceBegin = 0;
        int64_t inferenceEnd = 0;
        int64_t analyzerBegin = 0;
        int64_t analyzerEnd = 0;
        int64_t publish = 0;
    };

    bool parseLine(const std::string& line, Timeline& outTimeline);
    Statistics computeStats(const std::vector<int64_t>& samples) const;
    static double computeStdDev(const std::vector<int64_t>& samples, double mean);
    
    std::string logFile_;
    std::vector<Timeline> timelines_;
    // vectors of raw samples for each stage and transition
    std::vector<int64_t> captureSamples_, inferenceBeginSamples_, inferenceEndSamples_, analyzerBeginSamples_, analyzerEndSamples_, publishSamples_;
    std::vector<int64_t> capToInfBegin_, infBeginToEnd_, infEndToAnaBegin_, anaBeginToEnd_, anaEndToPub_, capToPub_;
};

} // namespace traffic_perception
