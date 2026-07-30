#pragma once

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace traffic_perception {

constexpr std::uint8_t NUM_LANES = 4;

class TimelineAnalyzer {
 public:
  explicit TimelineAnalyzer(const std::string& logFile)
      : logFile_(logFile) {}

  bool load();
  bool analyze();

  void analyzeLane(int laneId) const;
  void printReport(std::ostream& out = std::cout) const;

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

  enum class TimelineType {
    Stream,
    Pipeline,
    Viewer,
    Unknown
  };

  struct Timeline {
    TimelineType type = TimelineType::Unknown;

    int laneId = -1;
    uint32_t frameId = 0;
    uint32_t cycleId = 0;

    int64_t expectedWakeup = 0;
    int64_t begin = 0;
    int64_t end = 0;
  };

 private:
  bool parseLine(const std::string& line, Timeline& timeline);

  Statistics computeStats(
      const std::vector<int64_t>& samples) const;

  static double computeStdDev(
      const std::vector<int64_t>& samples,
      double mean);

  struct AnalysisData {
    std::vector<int64_t> wakeupLatency;
    std::vector<int64_t> executionTime;
    std::vector<int64_t> period;
  };

  std::string logFile_;

  std::vector<Timeline> timelines_;

  AnalysisData streamData_[NUM_LANES];
  AnalysisData pipelineData_;
  AnalysisData viewerData_;
};

}  // namespace traffic_perception
