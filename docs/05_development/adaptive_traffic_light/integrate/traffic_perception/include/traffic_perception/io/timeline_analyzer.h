#pragma once

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "traffic_perception/core/types.h"

namespace traffic_perception {

// ─────────────────────────────────────────────────────────────────────────────
// Statistics
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
// Internal data model (used by TimelineAnalyzer)
// ─────────────────────────────────────────────────────────────────────────────

enum class TimelineType { Unknown, Stream, Pipeline, Viewer };

struct Timeline {
  TimelineType type = TimelineType::Unknown;

  // Stream fields
  int laneId = -1;
  uint32_t frameId = 0;

  // Pipeline / Viewer fields
  uint32_t cycleId = 0;

  // Common timing fields (nanoseconds, CLOCK_MONOTONIC)
  int64_t expectedWakeup = 0;
  int64_t wakeup = 0;
  int64_t begin = 0;
  int64_t end = 0;
};

struct AnalysisData {
  std::vector<int64_t> schedulerLatency;
  std::vector<int64_t> dispatchLatency;
  std::vector<int64_t> responseTime;
  std::vector<int64_t> executionTime;
  std::vector<int64_t> period;
  std::vector<int64_t> deadlineMiss;
};

// ─────────────────────────────────────────────────────────────────────────────
// TimelineAnalyzer
// ─────────────────────────────────────────────────────────────────────────────

class TimelineAnalyzer {
 public:
  explicit TimelineAnalyzer(std::string logPath);

  /// Parse the log file. Returns false if the file cannot be opened or is
  /// empty.
  bool load();

  /// Compute per-lane and aggregate statistics from the parsed timelines.
  /// Must be called after a successful load(). Returns false if there is no
  /// data to analyze.
  bool analyze();

  /// Print a formatted report to @p out and also write it to
  /// <logFile>_statistics.txt.
  void printReport(std::ostream& out = std::cout) const;

  /// Print per-lane statistics for @p laneId to stdout.
  void analyzeLane(int laneId) const;

  static Statistics computeStats(const std::vector<int64_t>& samples);

  static double computeStdDev(const std::vector<int64_t>& samples,
                               double mean);

 private:
  static bool parseLine(const std::string& line, Timeline& outTimeline);

 private:
  std::string logFile_;

  // ── Parsed timelines ─────────────────────────────────────────────────────
  std::vector<Timeline> timelines_;

  // ── Per-lane stream analysis results ────────────────────────────────────
  std::array<AnalysisData, NUM_LANES> streamData_;

  // ── Pipeline analysis results ────────────────────────────────────────────
  AnalysisData pipelineData_;

  // ── Viewer analysis results ──────────────────────────────────────────────
  AnalysisData viewerData_;
};

}  // namespace traffic_perception
