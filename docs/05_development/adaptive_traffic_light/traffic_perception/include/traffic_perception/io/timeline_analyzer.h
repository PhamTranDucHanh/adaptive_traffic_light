#pragma once

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "traffic_perception/core/types.h"

namespace traffic_perception {

struct TaskTimeline {
  uint32_t sequence = 0;

  int64_t expectedWakeup = -1;
  int64_t begin = -1;
  int64_t end = -1;
};

struct StreamTimeline {
  uint32_t frameId = 0;
  int32_t laneId = -1;

  int64_t expectedWakeup = -1;
  int64_t begin = -1;
  int64_t end = -1;
};

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

class TimelineAnalyzer {
 public:
  bool load(const std::string& logPath);
  bool analyze();
  void printReport(std::ostream& out = std::cout) const;

 private:
  Statistics computeStats(const std::vector<int64_t>& samples) const;
  void analyzeStreamLane(const std::vector<StreamTimeline>& timelines);
  void analyzeTask(const std::vector<TaskTimeline>& timelines);

 private:
  std::string logPath_;

  //----------------------------------------------------------------------
  // Parsed timelines
  //----------------------------------------------------------------------

  std::array<std::vector<StreamTimeline>, NUM_LANES> streamTimelines_;
  std::vector<TaskTimeline> pipelineTimelines_;
  std::vector<TaskTimeline> viewerTimelines_;

  //----------------------------------------------------------------------
  // Stream statistics (per lane)
  //----------------------------------------------------------------------

  std::array<std::vector<int64_t>, NUM_LANES> streamWakeupLatency_;
  std::array<std::vector<int64_t>, NUM_LANES> streamExecutionTime_;
  std::array<std::vector<int64_t>, NUM_LANES> streamPeriod_;

  //----------------------------------------------------------------------
  // Pipeline statistics
  //----------------------------------------------------------------------

  std::vector<int64_t> pipelineWakeupLatency_;
  std::vector<int64_t> pipelineExecutionTime_;
  std::vector<int64_t> pipelinePeriod_;

  //----------------------------------------------------------------------
  // Viewer statistics
  //----------------------------------------------------------------------

  std::vector<int64_t> viewerWakeupLatency_;
  std::vector<int64_t> viewerExecutionTime_;
  std::vector<int64_t> viewerPeriod_;
};

}  // namespace traffic_perception
