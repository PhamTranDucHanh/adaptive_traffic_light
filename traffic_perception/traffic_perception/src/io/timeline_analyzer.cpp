#include "traffic_perception/io/timeline_analyzer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "timeline_analyzer/statistics.h"

namespace traffic_perception {

namespace {

constexpr double kNanoPerMicro = 1'000.0;
constexpr double kNanoPerMilli = 1'000'000.0;
constexpr double kNanoPerSecond = 1'000'000'000.0;

struct ScaleInfo {
  double factor;
  const char* unit;
};

ScaleInfo GetBestScale(double valueNs) {
  const double absValue = std::abs(valueNs);

  if (absValue >= kNanoPerSecond) {
    return {kNanoPerSecond, "s"};
  }

  if (absValue >= kNanoPerMilli) {
    return {kNanoPerMilli, "ms"};
  }

  if (absValue >= kNanoPerMicro) {
    return {kNanoPerMicro, "us"};
  }

  return {1.0, "ns"};
}

bool ContainsTag(const std::string& line, const std::string& tag) {
  const std::string token = " " + tag + " ";
  return line.find(token) != std::string::npos;
}

bool ExtractInt64(const std::string& line,
                  const std::string& key,
                  int64_t& value) {
  const size_t pos = line.find(key);
  if (pos == std::string::npos) {
    return false;
  }

  size_t start = pos + key.size();
  while (start < line.size() &&
         std::isspace(static_cast<unsigned char>(line[start]))) {
    ++start;
  }

  size_t end = start;
  if (end < line.size() && (line[end] == '-' || line[end] == '+')) {
    ++end;
  }

  while (end < line.size() &&
         std::isdigit(static_cast<unsigned char>(line[end]))) {
    ++end;
  }

  if (end == start || (end == start + 1 &&
                       (line[start] == '-' || line[start] == '+'))) {
    return false;
  }

  try {
    value = std::stoll(line.substr(start, end - start));
    return true;
  } catch (...) {
    return false;
  }
}

bool ExtractUInt32(const std::string& line,
                   const std::string& key,
                   uint32_t& value) {
  int64_t parsed = 0;
  if (!ExtractInt64(line, key, parsed) || parsed < 0) {
    return false;
  }

  value = static_cast<uint32_t>(parsed);
  return true;
}

double MaxAbsMean(const timeline_analyzer::Statistics& s) {
  return std::max(std::abs(s.mean),
                  std::max(std::abs(static_cast<double>(s.min)),
                           std::abs(static_cast<double>(s.max))));
}

double ComputeStdDevInternal(const std::vector<int64_t>& samples,
                             double mean) {
  if (samples.empty()) {
    return 0.0;
  }

  double sum = 0.0;
  for (const int64_t sample : samples) {
    const double diff = static_cast<double>(sample) - mean;
    sum += diff * diff;
  }
  return std::sqrt(sum / static_cast<double>(samples.size()));
}

struct NamedSamples {
  std::string name;
  const std::vector<int64_t>* values;
};

using NamedDataset = std::vector<NamedSamples>;

bool HasAnyData(const NamedDataset& datasets) {
  for (const auto& dataset : datasets) {
    if (dataset.values != nullptr && !dataset.values->empty()) {
      return true;
    }
  }
  return false;
}

ScaleInfo DetermineScale(const NamedDataset& datasets) {
  double maxAbs = 0.0;
  for (const auto& dataset : datasets) {
    if (dataset.values == nullptr || dataset.values->empty()) {
      continue;
    }

    const auto stats = timeline_analyzer::ComputeStatistics(*dataset.values);
    maxAbs = std::max(maxAbs, MaxAbsMean(stats));
  }
  return GetBestScale(maxAbs);
}

void PrintHeader(std::ostream& out) {
  out << std::setw(24) << std::left << "Stage" << std::setw(10) << "Count"
      << std::setw(14) << "Min" << std::setw(14) << "Avg" << std::setw(14)
      << "P50" << std::setw(14) << "P90" << std::setw(14) << "P95"
      << std::setw(14) << "P99" << std::setw(14) << "Max" << std::setw(14)
      << "Stddev" << "\n"
      << std::string(132, '-') << "\n";
}

void PrintRow(std::ostream& out,
              const std::string& name,
              const timeline_analyzer::Statistics& s,
              double stddev,
              const ScaleInfo& scale) {
  if (s.sampleCount == 0) {
    return;
  }

  const auto format = [&](double value) { return value / scale.factor; };

  out << std::setw(24) << std::left << name << std::setw(10) << s.sampleCount
      << std::fixed << std::setprecision(3) << std::setw(14)
      << format(static_cast<double>(s.min)) << std::setw(14) << format(s.mean)
      << std::setw(14) << format(static_cast<double>(s.median))
      << std::setw(14) << format(static_cast<double>(s.p90)) << std::setw(14)
      << format(static_cast<double>(s.p95)) << std::setw(14)
      << format(static_cast<double>(s.p99)) << std::setw(14)
      << format(static_cast<double>(s.max)) << std::setw(14)
      << format(stddev) << "\n";
}

void PrintSection(std::ostream& out,
                  const std::string& title,
                  const NamedDataset& datasets) {
  if (!HasAnyData(datasets)) {
    return;
  }

  const ScaleInfo scale = DetermineScale(datasets);

  out << title << " (" << scale.unit << ")\n";
  PrintHeader(out);

  for (const auto& dataset : datasets) {
    if (dataset.values == nullptr || dataset.values->empty()) {
      continue;
    }

    const auto stats = timeline_analyzer::ComputeStatistics(*dataset.values);
    const double stddev = ComputeStdDevInternal(*dataset.values, stats.mean);
    PrintRow(out, dataset.name, stats, stddev, scale);
  }

  out << "\n";
}
}  // namespace

bool TimelineAnalyzer::load() {
  timelines_.clear();

  std::ifstream in;
  std::filesystem::path path(logFile_);
  if (std::filesystem::exists(path)) {
    in.open(path);
  } else {
    auto runfiles_path = std::filesystem::path("runfiles") / logFile_;
    if (std::filesystem::exists(runfiles_path)) {
      in.open(runfiles_path);
      logFile_ = runfiles_path.string();
    } else {
      auto current_path = std::filesystem::current_path() / logFile_;
      if (std::filesystem::exists(current_path)) {
        in.open(current_path);
        logFile_ = current_path.string();
      } else {
        auto dir = std::filesystem::current_path();
        while (dir.has_parent_path()) {
          if (std::filesystem::exists(dir / "WORKSPACE")) {
            auto workspace_path = dir / logFile_;
            if (std::filesystem::exists(workspace_path)) {
              in.open(workspace_path);
              logFile_ = workspace_path.string();
            }
            break;
          }
          dir = dir.parent_path();
        }
      }
    }
  }
  if (!in.is_open()) return false;

  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }

    Timeline tl;
    if (parseLine(line, tl)) {
      timelines_.push_back(tl);
    }
  }

  return !timelines_.empty();
}

bool TimelineAnalyzer::analyze() {
  for (auto& data : streamData_) {
    data.wakeupLatency.clear();
    data.executionTime.clear();
    data.period.clear();
  }
  pipelineData_.wakeupLatency.clear();
  pipelineData_.executionTime.clear();
  pipelineData_.period.clear();
  viewerData_.wakeupLatency.clear();
  viewerData_.executionTime.clear();
  viewerData_.period.clear();

  if (timelines_.empty()) {
    return false;
  }

  std::array<int64_t, NUM_LANES> lastStreamBegin{};
  int64_t lastPipelineBegin = 0;
  int64_t lastViewerBegin = 0;

  for (const auto& tl : timelines_) {
    switch (tl.type) {
      case TimelineType::Stream:
        if (tl.laneId >= 0 && tl.laneId < NUM_LANES) {
          AnalysisData& data = streamData_[static_cast<size_t>(tl.laneId)];
          const size_t laneIndex = static_cast<size_t>(tl.laneId);
          if (tl.begin != 0 && tl.end != 0 && tl.end >= tl.begin) {
            data.executionTime.push_back(tl.end - tl.begin);
            if (tl.expectedWakeup != 0) {
              data.wakeupLatency.push_back(tl.begin - tl.expectedWakeup);
            }
            if (lastStreamBegin[laneIndex] != 0 &&
                tl.begin >= lastStreamBegin[laneIndex]) {
              data.period.push_back(tl.begin - lastStreamBegin[laneIndex]);
            }
            lastStreamBegin[laneIndex] = tl.begin;
          }
        }
        break;

      case TimelineType::Pipeline:
        if (tl.begin != 0 && tl.end != 0 && tl.end >= tl.begin) {
          pipelineData_.executionTime.push_back(tl.end - tl.begin);
          if (tl.expectedWakeup != 0) {
            pipelineData_.wakeupLatency.push_back(tl.begin - tl.expectedWakeup);
          }
          if (lastPipelineBegin != 0 && tl.begin >= lastPipelineBegin) {
            pipelineData_.period.push_back(tl.begin - lastPipelineBegin);
          }
          lastPipelineBegin = tl.begin;
        }
        break;

      case TimelineType::Viewer:
        if (tl.begin != 0 && tl.end != 0 && tl.end >= tl.begin) {
          viewerData_.executionTime.push_back(tl.end - tl.begin);
          if (tl.expectedWakeup != 0) {
            viewerData_.wakeupLatency.push_back(tl.begin - tl.expectedWakeup);
          }
          if (lastViewerBegin != 0 && tl.begin >= lastViewerBegin) {
            viewerData_.period.push_back(tl.begin - lastViewerBegin);
          }
          lastViewerBegin = tl.begin;
        }
        break;

      case TimelineType::Unknown:
      default:
        break;
    }
  }

  return true;
}

void TimelineAnalyzer::printReport(std::ostream& out) const {
  AnalysisData streamAll;
  for (const auto& laneData : streamData_) {
    streamAll.wakeupLatency.insert(streamAll.wakeupLatency.end(),
                                   laneData.wakeupLatency.begin(),
                                   laneData.wakeupLatency.end());
    streamAll.executionTime.insert(streamAll.executionTime.end(),
                                   laneData.executionTime.begin(),
                                   laneData.executionTime.end());
    streamAll.period.insert(streamAll.period.end(),
                            laneData.period.begin(),
                            laneData.period.end());
  }

  std::stringstream ss;
  ss << "====================================================================\n";
  ss << "Timeline Analyzer Report\n";
  ss << "Total records: " << timelines_.size() << "\n\n";

  PrintSection(ss,
               "Wake-up Latency",
               {
                   {"Stream Worker", &streamAll.wakeupLatency},
                   {"Pipeline", &pipelineData_.wakeupLatency},
                   {"Viewer", &viewerData_.wakeupLatency},
               });

  PrintSection(ss,
               "Execution Time",
               {
                   {"Stream Worker", &streamAll.executionTime},
                   {"Pipeline", &pipelineData_.executionTime},
                   {"Viewer", &viewerData_.executionTime},
               });

  PrintSection(ss,
               "Period",
               {
                   {"Stream Worker", &streamAll.period},
                   {"Pipeline", &pipelineData_.period},
                   {"Viewer", &viewerData_.period},
               });

  ss << "====================================================================\n";

  std::string report = ss.str();
  out << report;

  std::ofstream reportFile(logFile_ + "_statistics.txt");
  if (reportFile) {
    reportFile << report;
  }
}

TimelineAnalyzer::Statistics TimelineAnalyzer::computeStats(
    const std::vector<int64_t>& samples) const {
  auto s = timeline_analyzer::ComputeStatistics(samples);
  TimelineAnalyzer::Statistics ts;
  ts.min = s.min;
  ts.max = s.max;
  ts.mean = s.mean;
  ts.median = s.median;
  ts.p90 = s.p90;
  ts.p95 = s.p95;
  ts.p99 = s.p99;
  ts.sampleCount = s.sampleCount;
  ts.stddev = computeStdDev(samples, s.mean);
  return ts;
}

double TimelineAnalyzer::computeStdDev(const std::vector<int64_t>& samples,
                                       double mean) {
  return ComputeStdDevInternal(samples, mean);
}

void TimelineAnalyzer::analyzeLane(int laneId) const {
  if (laneId < 0 || laneId >= NUM_LANES) {
    std::cout << "Invalid lane id: " << laneId << "\n";
    return;
  }

  const AnalysisData& data = streamData_[static_cast<size_t>(laneId)];
  if (data.executionTime.empty() && data.wakeupLatency.empty() &&
      data.period.empty()) {
    std::cout << "No data for lane " << laneId << "\n";
    return;
  }

  std::cout << "=== Lane " << laneId << " Statistics ===\n";
  PrintSection(std::cout,
               "Wake-up Latency",
               {
                   {"Stream Lane " + std::to_string(laneId), &data.wakeupLatency},
               });
  PrintSection(std::cout,
               "Execution Time",
               {
                   {"Stream Lane " + std::to_string(laneId), &data.executionTime},
               });
  PrintSection(std::cout,
               "Period",
               {
                   {"Stream Lane " + std::to_string(laneId), &data.period},
               });
}

bool TimelineAnalyzer::parseLine(const std::string& line,
                                 Timeline& outTimeline) {
  outTimeline = Timeline();

  if (ContainsTag(line, "STRM")) {
    outTimeline.type = TimelineType::Stream;
  } else if (ContainsTag(line, "PIPE")) {
    outTimeline.type = TimelineType::Pipeline;
  } else if (ContainsTag(line, "VIEW")) {
    outTimeline.type = TimelineType::Viewer;
  } else {
    return false;
  }

  if (!ExtractInt64(line, "Begin=", outTimeline.begin) ||
      !ExtractInt64(line, "End=", outTimeline.end)) {
    return false;
  }

  ExtractInt64(line, "ExpectedWakeup=", outTimeline.expectedWakeup);

  if (outTimeline.type == TimelineType::Stream) {
    int64_t laneId = -1;
    if (!ExtractInt64(line, "LaneId=", laneId)) {
      return false;
    }
    outTimeline.laneId = static_cast<int>(laneId);

    if (!ExtractUInt32(line, "FrameId=", outTimeline.frameId)) {
      return false;
    }
  }

  return true;
}

}  // namespace traffic_perception
