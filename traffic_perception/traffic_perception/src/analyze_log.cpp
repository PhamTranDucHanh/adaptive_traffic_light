#include <iostream>
#include "traffic_perception/io/timeline_analyzer.h"
#include <filesystem>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <log_file1> [log_file2 ...]" << std::endl;
        return 1;
    }
    // Process each provided log file
    for (int i = 1; i < argc; ++i) {
        std::string logPath = argv[i];
        std::cout << "=== Analyzing " << logPath << " ===" << std::endl;
        traffic_perception::TimelineAnalyzer analyzer(logPath);
        if (!analyzer.load()) {
            std::cerr << "Failed to load log file: " << logPath << std::endl;
            continue;
        }
        if (!analyzer.analyze()) {
            std::cerr << "Failed to analyze log file: " << logPath << std::endl;
            continue;
        }
        analyzer.printReport(std::cout);
        std::cout << std::endl;
        const int numLanes = 4; // adjust if needed
        for (int lane = 0; lane < numLanes; ++lane) {
            std::cout << "--- Lane " << lane << " Statistics ---" << std::endl;
            analyzer.analyzeLane(lane);
        }
    }
    return 0;
}
