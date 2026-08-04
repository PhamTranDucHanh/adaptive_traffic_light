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

namespace traffic_perception {

namespace {

constexpr double kNanoPerMicro = 1'000.0;
constexpr double kNanoPerMilli = 1'000'000.0;
constexpr double kNanoPerSecond = 1'000'000'000.0;

constexpr int64_t kStreamDeadlineNs =
    100LL * 1000 * 1000;

constexpr int64_t kPipelineDeadlineNs =
    5LL * 1000 * 1000 * 1000;

constexpr int64_t kViewerDeadlineNs =
    5LL * 1000 * 1000 * 1000;

constexpr uint32_t kWarmupFramesToSkip = 15;

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

std::string FormatLinuxTime(double valueNs) {
  ScaleInfo scale = GetBestScale(valueNs);
  double scaled = valueNs / scale.factor;

  std::ostringstream oss;
  if (std::abs(scaled) < 10.0) {
    oss << std::fixed << std::setprecision(1);
  } else {
    oss << std::fixed << std::setprecision(0);
  }
  oss << scaled << scale.unit;
  return oss.str();
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
              const Statistics& s) {
  if (s.sampleCount == 0) {
    return;
  }

  out << std::setw(24) << std::left << name << std::setw(10) << s.sampleCount
      << std::setw(14) << FormatLinuxTime(static_cast<double>(s.min))
      << std::setw(14) << FormatLinuxTime(s.mean)
      << std::setw(14) << FormatLinuxTime(static_cast<double>(s.median))
      << std::setw(14) << FormatLinuxTime(static_cast<double>(s.p90))
      << std::setw(14) << FormatLinuxTime(static_cast<double>(s.p95))
      << std::setw(14) << FormatLinuxTime(static_cast<double>(s.p99))
      << std::setw(14) << FormatLinuxTime(static_cast<double>(s.max))
      << std::setw(14) << FormatLinuxTime(s.stddev) << "\n";
}

void PrintSection(std::ostream& out,
                  const std::string& title,
                  const NamedDataset& datasets) {
  if (!HasAnyData(datasets)) {
    return;
  }

  out << title << "\n";
  PrintHeader(out);

  for (const auto& dataset : datasets) {
    if (dataset.values == nullptr || dataset.values->empty()) {
      continue;
    }

    const auto stats = TimelineAnalyzer::computeStats(*dataset.values);
    PrintRow(out, dataset.name, stats);
  }

  out << "\n";
}
}  // namespace

TimelineAnalyzer::TimelineAnalyzer(std::string logPath) : logFile_(std::move(logPath)) {}

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
    data.schedulerLatency.clear();
    data.dispatchLatency.clear();
    data.responseTime.clear();
    data.executionTime.clear();
    data.period.clear();
    data.deadlineMiss.clear();
  }
  pipelineData_.schedulerLatency.clear();
  pipelineData_.dispatchLatency.clear();
  pipelineData_.responseTime.clear();
  pipelineData_.executionTime.clear();
  pipelineData_.period.clear();
  pipelineData_.deadlineMiss.clear();
  viewerData_.schedulerLatency.clear();
  viewerData_.dispatchLatency.clear();
  viewerData_.responseTime.clear();
  viewerData_.executionTime.clear();
  viewerData_.period.clear();
  viewerData_.deadlineMiss.clear();

  if (timelines_.empty()) {
    return false;
  }

  std::array<int64_t, NUM_LANES> lastStreamBegin{};
  int64_t lastPipelineBegin = 0;
  int64_t lastViewerBegin = 0;

  for (const auto& tl : timelines_) {
    switch (tl.type) {
      case TimelineType::Stream:
        if (tl.frameId <= kWarmupFramesToSkip) {
          break;
        }

        if (tl.laneId >= 0 && tl.laneId < static_cast<int>(NUM_LANES)) {
          AnalysisData& data = streamData_[static_cast<size_t>(tl.laneId)];
          const size_t laneIndex = static_cast<size_t>(tl.laneId);

          if (tl.begin != 0 && tl.end != 0 && tl.end >= tl.begin) {
              data.executionTime.push_back(tl.end - tl.begin);

              if (tl.expectedWakeup != 0) {
                  data.schedulerLatency.push_back(tl.begin - tl.expectedWakeup);

                  const int64_t responseTime =
                      tl.end - tl.expectedWakeup;

                  data.responseTime.push_back(
                      responseTime);

                  data.deadlineMiss.push_back(
                      std::max<int64_t>(
                          0,
                          responseTime - kStreamDeadlineNs));
              }

              if (lastStreamBegin[laneIndex] != 0 &&
                  tl.begin >= lastStreamBegin[laneIndex]) {
                  data.period.push_back(
                      tl.begin - lastStreamBegin[laneIndex]);
              }

              lastStreamBegin[laneIndex] = tl.begin;
          }
        }
        break;

      case TimelineType::Pipeline:
        if (tl.cycleId != 0 && tl.cycleId <= kWarmupFramesToSkip) {
          break;
        }

        if (tl.begin != 0 && tl.end != 0 && tl.end >= tl.begin) {
            pipelineData_.executionTime.push_back(tl.end - tl.begin);

            if (tl.expectedWakeup != 0) {
                pipelineData_.schedulerLatency.push_back(tl.begin - tl.expectedWakeup);

                const int64_t responseTime =
                    tl.end - tl.expectedWakeup;

                pipelineData_.responseTime.push_back(
                    responseTime);

                pipelineData_.deadlineMiss.push_back(
                    std::max<int64_t>(
                        0,
                        responseTime - kPipelineDeadlineNs));
            }

            if (lastPipelineBegin != 0 &&
                tl.begin >= lastPipelineBegin) {
                pipelineData_.period.push_back(
                    tl.begin - lastPipelineBegin);
            }

            lastPipelineBegin = tl.begin;
        }

        break;

      case TimelineType::Viewer:
        if (tl.cycleId != 0 && tl.cycleId <= kWarmupFramesToSkip) {
          break;
        }

        if (tl.begin != 0 && tl.end != 0 && tl.end >= tl.begin) {
            viewerData_.executionTime.push_back(tl.end - tl.begin);

            if (tl.expectedWakeup != 0) {
                viewerData_.schedulerLatency.push_back(tl.begin - tl.expectedWakeup);

                const int64_t responseTime =
                    tl.end - tl.expectedWakeup;

                viewerData_.responseTime.push_back(
                    responseTime);

                viewerData_.deadlineMiss.push_back(
                    std::max<int64_t>(
                        0,
                        responseTime - kViewerDeadlineNs));
            }

            if (lastViewerBegin != 0 &&
                tl.begin >= lastViewerBegin) {
                viewerData_.period.push_back(
                    tl.begin - lastViewerBegin);
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
    streamAll.schedulerLatency.insert(streamAll.schedulerLatency.end(),
                                      laneData.schedulerLatency.begin(),
                                      laneData.schedulerLatency.end());
    streamAll.dispatchLatency.insert(streamAll.dispatchLatency.end(),
                                     laneData.dispatchLatency.begin(),
                                     laneData.dispatchLatency.end());
    streamAll.responseTime.insert(streamAll.responseTime.end(),
                                  laneData.responseTime.begin(),
                                  laneData.responseTime.end());
    streamAll.deadlineMiss.insert(streamAll.deadlineMiss.end(),
                                  laneData.deadlineMiss.begin(),
                                  laneData.deadlineMiss.end());
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
               "Scheduler Wake-up Latency",
               {
                   {"Stream Worker", &streamAll.schedulerLatency},
                   {"Pipeline", &pipelineData_.schedulerLatency},
                   {"Viewer", &viewerData_.schedulerLatency},
               });

  PrintSection(ss,
               "Dispatch Latency",
               {
                   {"Stream Worker", &streamAll.dispatchLatency},
                   {"Pipeline", &pipelineData_.dispatchLatency},
                   {"Viewer", &viewerData_.dispatchLatency},
               });

  PrintSection(ss,
               "Response Time",
               {
                   {"Stream Worker", &streamAll.responseTime},
                   {"Pipeline", &pipelineData_.responseTime},
                   {"Viewer", &viewerData_.responseTime},
               });

  PrintSection(ss,
               "Deadline Miss",
               {
                   {"Stream Worker", &streamAll.deadlineMiss},
                   {"Pipeline", &pipelineData_.deadlineMiss},
                   {"Viewer", &viewerData_.deadlineMiss},
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

Statistics TimelineAnalyzer::computeStats(
    const std::vector<int64_t>& samples) {
  Statistics ts;
  if (samples.empty()) {
    return ts;
  }

  std::vector<int64_t> sorted = samples;
  std::sort(sorted.begin(), sorted.end());

  ts.sampleCount = sorted.size();
  ts.min = sorted.front();
  ts.max = sorted.back();

  double sum = 0.0;
  for (int64_t v : sorted) {
    sum += static_cast<double>(v);
  }
  ts.mean = sum / static_cast<double>(ts.sampleCount);

  ts.median = sorted[ts.sampleCount / 2];
  ts.p90 = sorted[(ts.sampleCount * 90) / 100];
  ts.p95 = sorted[(ts.sampleCount * 95) / 100];
  ts.p99 = sorted[(ts.sampleCount * 99) / 100];
  ts.stddev = computeStdDev(samples, ts.mean);
  return ts;
}

double TimelineAnalyzer::computeStdDev(const std::vector<int64_t>& samples,
                                       double mean) {
  return ComputeStdDevInternal(samples, mean);
}

void TimelineAnalyzer::analyzeLane(int laneId) const {
  if (laneId < 0 || laneId >= static_cast<int>(NUM_LANES)) {
    std::cout << "Invalid lane id: " << laneId << "\n";
    return;
  }

  const AnalysisData& data = streamData_[static_cast<size_t>(laneId)];
  if (data.executionTime.empty() && data.schedulerLatency.empty() &&
      data.responseTime.empty() && data.deadlineMiss.empty() && data.period.empty()) {
    std::cout << "No data for lane " << laneId << "\n";
    return;
  }

  std::cout << "=== Lane " << laneId << " Statistics ===\n";
  PrintSection(std::cout,
               "Scheduler Wake-up Latency",
               {
                   {"Stream Lane " + std::to_string(laneId), &data.schedulerLatency},
               });
  PrintSection(std::cout,
               "Response Time",
               {
                   {"Stream Lane " + std::to_string(laneId), &data.responseTime},
               });
  PrintSection(std::cout,
               "Deadline Miss",
               {
                   {"Stream Lane " + std::to_string(laneId), &data.deadlineMiss},
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

  if (!ExtractInt64(line, "ExpectedWakeup=", outTimeline.expectedWakeup)) {
    return false;
  }

  ExtractInt64(line, "Wakeup=", outTimeline.wakeup);

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

  if (outTimeline.type == TimelineType::Pipeline ||
      outTimeline.type == TimelineType::Viewer) {
    ExtractUInt32(line, "CycleId=", outTimeline.cycleId);
  }

  return true;
}

}  // namespace traffic_perception
