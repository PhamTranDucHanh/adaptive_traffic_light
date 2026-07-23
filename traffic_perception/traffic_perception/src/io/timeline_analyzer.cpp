#include "traffic_perception/core/types.h"
#include "traffic_perception/io/timeline_analyzer.h"
#include "traffic_perception/timeline_analyzer/statistics.h"

#include <cmath>
#include <iomanip>
#include <numeric>
#include <map>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <vector>

namespace traffic_perception {

constexpr int64_t kNanoToMicro = 1000;

namespace {

void PrintRow(std::ostream& out, const std::string& name, const timeline_analyzer::Statistics& s) {
    if (s.sampleCount == 0) return;
    out << std::setw(30) << std::left << name
        << std::setw(15) << s.min
        << std::setw(15) << static_cast<int64_t>(std::round(s.mean))
        << std::setw(15) << s.median
        << std::setw(15) << s.p90
        << std::setw(15) << s.p95
        << std::setw(15) << s.p99
        << std::setw(15) << s.max
        << std::setw(15) << s.sampleCount << "\n";
}

void PrintHeader(std::ostream& out) {
    out << std::setw(30) << std::left << "Stage"
        << std::setw(15) << "Min"
        << std::setw(15) << "Avg"
        << std::setw(15) << "P50"
        << std::setw(15) << "P90"
        << std::setw(15) << "P95"
        << std::setw(15) << "P99"
        << std::setw(15) << "Max"
        << std::setw(15) << "Count" << "\n";
}

struct AnalysisData {
    std::vector<int64_t> capture;
    std::vector<int64_t> inferenceBegin;
    std::vector<int64_t> inferenceEnd;
    std::vector<int64_t> analyzerBegin;
    std::vector<int64_t> analyzerEnd;
    std::vector<int64_t> publish;

    std::vector<int64_t> capToInfBegin;
    std::vector<int64_t> infEndToAnaBegin;
    std::vector<int64_t> anaEndToPub;
    std::vector<int64_t> capToPub;

    std::vector<int64_t> infProc;
    std::vector<int64_t> anaProc;
};

void PopulateData(const std::vector<TimelineAnalyzer::Timeline>& timelines, AnalysisData& data, int laneId = -1) {
    for (const auto& tl : timelines) {
        if (laneId != -1 && tl.laneId != laneId) continue;

        if (tl.capture != 0) data.capture.push_back(tl.capture / kNanoToMicro);
        if (tl.inferenceBegin != 0) data.inferenceBegin.push_back(tl.inferenceBegin / kNanoToMicro);
        if (tl.inferenceEnd != 0) data.inferenceEnd.push_back(tl.inferenceEnd / kNanoToMicro);
        if (tl.analyzerBegin != 0) data.analyzerBegin.push_back(tl.analyzerBegin / kNanoToMicro);
        if (tl.analyzerEnd != 0) data.analyzerEnd.push_back(tl.analyzerEnd / kNanoToMicro);
        if (tl.publish != 0) data.publish.push_back(tl.publish / kNanoToMicro);

        if (tl.inferenceBegin != 0 && tl.capture != 0) data.capToInfBegin.push_back((tl.inferenceBegin - tl.capture) / kNanoToMicro);
        if (tl.analyzerBegin != 0 && tl.inferenceEnd != 0) data.infEndToAnaBegin.push_back((tl.analyzerBegin - tl.inferenceEnd) / kNanoToMicro);
        if (tl.publish != 0 && tl.analyzerEnd != 0) data.anaEndToPub.push_back((tl.publish - tl.analyzerEnd) / kNanoToMicro);
        if (tl.publish != 0 && tl.capture != 0) data.capToPub.push_back((tl.publish - tl.capture) / kNanoToMicro);

        if (tl.inferenceBegin != 0 && tl.inferenceEnd != 0) data.infProc.push_back((tl.inferenceEnd - tl.inferenceBegin) / kNanoToMicro);
        if (tl.analyzerBegin != 0 && tl.analyzerEnd != 0) data.anaProc.push_back((tl.analyzerEnd - tl.analyzerBegin) / kNanoToMicro);
    }
}

void PrintAnalysis(std::ostream& out, const AnalysisData& data) {
    out << "Absolute Timestamp Statistics (us)\n\n";
    PrintHeader(out);
    PrintRow(out, "Capture", timeline_analyzer::ComputeStatistics(data.capture));
    PrintRow(out, "InferenceBegin", timeline_analyzer::ComputeStatistics(data.inferenceBegin));
    PrintRow(out, "InferenceEnd", timeline_analyzer::ComputeStatistics(data.inferenceEnd));
    PrintRow(out, "AnalyzerBegin", timeline_analyzer::ComputeStatistics(data.analyzerBegin));
    PrintRow(out, "AnalyzerEnd", timeline_analyzer::ComputeStatistics(data.analyzerEnd));
    PrintRow(out, "Publish", timeline_analyzer::ComputeStatistics(data.publish));

    out << "\nLatency Statistics (us)\n\n";
    PrintHeader(out);
    PrintRow(out, "Capture->InferenceBegin", timeline_analyzer::ComputeStatistics(data.capToInfBegin));
    PrintRow(out, "InferenceEnd->AnalyzerBegin", timeline_analyzer::ComputeStatistics(data.infEndToAnaBegin));
    PrintRow(out, "AnalyzerEnd->Publish", timeline_analyzer::ComputeStatistics(data.anaEndToPub));
    PrintRow(out, "Capture->Publish", timeline_analyzer::ComputeStatistics(data.capToPub));

    out << "\nProcess Time Statistics (us)\n\n";
    PrintHeader(out);
    PrintRow(out, "Inference", timeline_analyzer::ComputeStatistics(data.infProc));
    PrintRow(out, "Analyzer", timeline_analyzer::ComputeStatistics(data.anaProc));
}

} // namespace

bool TimelineAnalyzer::load() {
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
    Timeline currentTl;
    bool hasCurrentTl = false;

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        
        Timeline tl;
        if (parseLine(line, tl)) {
            timelines_.push_back(tl);
            hasCurrentTl = false;
            continue;
        }

        size_t frameIdPos = line.find("FrameId=");
        if (frameIdPos != std::string::npos) {
            if (hasCurrentTl) {
                timelines_.push_back(currentTl);
            }
            
            size_t start = frameIdPos + 8;
            while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) ++start;
            size_t end = line.find(' ', start);
            if (end == std::string::npos) end = line.find(']', start);
            
            currentTl = Timeline();
            try {
                currentTl.frameId = static_cast<uint32_t>(std::stoll(line.substr(start, end - start)));
                currentTl.laneId = static_cast<int>(currentTl.frameId % NUM_LANES);
                hasCurrentTl = true;
            } catch (...) {
                hasCurrentTl = false;
            }
            continue;
        }

        if (hasCurrentTl) {
            size_t eqPos = line.find('=');
            if (eqPos != std::string::npos) {
                size_t idxStart = line.rfind('[', eqPos);
                if (idxStart == std::string::npos) idxStart = 0;
                else idxStart += 1;
                
                std::string idxStr = line.substr(idxStart, eqPos - idxStart);
                idxStr.erase(0, idxStr.find_first_not_of(" \t"));
                idxStr.erase(idxStr.find_last_not_of(" \t") + 1);

                size_t valStart = eqPos + 1;
                while (valStart < line.size() && std::isspace(static_cast<unsigned char>(line[valStart]))) ++valStart;
                size_t valEnd = line.find(' ', valStart);
                if (valEnd == std::string::npos) valEnd = line.find(']', valStart);
                std::string valStr = line.substr(valStart, valEnd - valStart);

                try {
                    int64_t timestamp = std::stoll(valStr);
                    if (idxStr == "0") currentTl.capture = timestamp;
                    else if (idxStr == "1") currentTl.inferenceBegin = timestamp;
                    else if (idxStr == "2") currentTl.inferenceEnd = timestamp;
                    else if (idxStr == "3") currentTl.analyzerBegin = timestamp;
                    else if (idxStr == "4") currentTl.analyzerEnd = timestamp;
                    else if (idxStr == "5") currentTl.publish = timestamp;
                } catch (...) {
                }
                continue;
            }
        }

        size_t rbPos = line.find("RenderBegin=");
        size_t rePos = line.find("RenderEnd=");
        if (rbPos != std::string::npos && rePos != std::string::npos) {
            auto extract = [&](size_t pos, const std::string& key) -> int64_t {
                size_t start = pos + key.size();
                while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) ++start;
                size_t end = line.find(' ', start);
                if (end == std::string::npos) end = line.find(']', start);
                std::string numStr = line.substr(start, end - start);
                try {
                    return std::stoll(numStr);
                } catch (const std::exception&) {
                    return 0;
                }
            };
            int64_t renderBegin = extract(rbPos, "RenderBegin=");
            int64_t renderEnd = extract(rePos, "RenderEnd=");
            static uint32_t dummyId = 0;
            Timeline tl;
            tl.frameId = dummyId++;
            tl.laneId = 0;
            tl.capture = renderBegin;
            tl.publish = renderEnd;
            tl.inferenceBegin = tl.inferenceEnd = tl.analyzerBegin = tl.analyzerEnd = 0;
            timelines_.push_back(tl);
            hasCurrentTl = false;
        }
    }
    if (hasCurrentTl) {
        timelines_.push_back(currentTl);
    }
    return true;
}

bool TimelineAnalyzer::analyze() {
    return !timelines_.empty();
}

void TimelineAnalyzer::printReport(std::ostream& out) const {
    AnalysisData data;
    PopulateData(timelines_, data);

    out << "===================================================================================================================================================\n";
    out << "Frames analyzed: " << timelines_.size() << "\n\n";
    PrintAnalysis(out, data);
    out << "===================================================================================================================================================\n";

    std::ofstream reportFile(logFile_ + "_statistics.txt");
    if (reportFile) {
        reportFile << out.rdbuf();
    }
}

void TimelineAnalyzer::analyzeLane(int laneId) const {
    AnalysisData data;
    PopulateData(timelines_, data, laneId);

    if (data.capture.empty()) {
        std::cout << "No data for lane " << laneId << "\n";
        return;
    }

    std::cout << "=== Lane " << laneId << " Statistics ===\n";
    PrintAnalysis(std::cout, data);
}

bool TimelineAnalyzer::parseLine(const std::string& line, Timeline& outTimeline) {
    std::istringstream ss(line);
    std::string token;
    std::map<std::string, int64_t> values;
    while (ss >> token) {
        if (token.front() == '[') token = token.substr(1);
        if (!token.empty() && token.back() == ']') token.pop_back();
        if (token.empty()) continue;

        auto eqPos = token.find('=');
        if (eqPos == std::string::npos) continue;
        std::string key = token.substr(0, eqPos);
        std::string valStr = token.substr(eqPos + 1);

        if (valStr.empty()) {
            if (!(ss >> valStr)) {
                return false;
            }
            if (!valStr.empty() && valStr.back() == ']') valStr.pop_back();
        }
        try {
            int64_t v = std::stoll(valStr);
            values[key] = v;
        } catch (...) {
            return false;
        }
    }
    if (values.find("FrameId") == values.end()) return false;
    outTimeline.frameId = static_cast<uint32_t>(values["FrameId"]);
    outTimeline.laneId = static_cast<int>(outTimeline.frameId % NUM_LANES);
    outTimeline.capture = values.count("Capture") ? values["Capture"] : 0;
    outTimeline.inferenceBegin = values.count("InferenceBegin") ? values["InferenceBegin"] : 0;
    outTimeline.inferenceEnd = values.count("InferenceEnd") ? values["InferenceEnd"] : 0;
    outTimeline.analyzerBegin = values.count("AnalyzerBegin") ? values["AnalyzerBegin"] : 0;
    outTimeline.analyzerEnd = values.count("AnalyzerEnd") ? values["AnalyzerEnd"] : 0;
    outTimeline.publish = values.count("Publish") ? values["Publish"] : 0;
    return true;
}

} // namespace traffic_perception
