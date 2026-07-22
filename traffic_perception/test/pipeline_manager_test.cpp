#include <iostream>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <opencv2/opencv.hpp>
#include "traffic_perception/core/config_manager.h"
#include "traffic_perception/core/frame_pool.h"
#include "traffic_perception/ingestion/atomic_frame_buffer.h"
#include "traffic_perception/ingestion/stream_worker.h"
#include "traffic_perception/inference/yolov8_oiv7_backend.h"
#include "traffic_perception/pipeline/pipeline_manager.h"
#include <traffic_perception/inference/yolov8_backend.h>
#include "traffic_perception/pipeline/snapshot_publisher.h"
#include "traffic_perception/io/snapshot_sender.h"
#include "traffic_perception/viewer/opencv_lanes_viewer.h"
#include "tools/cpp/runfiles/runfiles.h"

using namespace traffic_perception;
using bazel::tools::cpp::runfiles::Runfiles;

int main(int argc, char* argv[]) {
    // Bazel Runfiles setup
    std::string error;
    std::unique_ptr<Runfiles> runfiles(Runfiles::Create(argv[0], &error));
    if (!runfiles) return 1;
    
    // Resolve config path via Runfiles
    std::string configPath = runfiles->Rlocation("traffic_perception/config/traffic_perception_config.json");
    if (configPath.empty()) return 1;
    
    // Load configuration
    ConfigManager configManager(configPath);
    configManager.loadConfig();
    AppConfig& config = configManager.getConfig();

    // Resolve paths post-load in-place
    config.modelPath = runfiles->Rlocation(config.modelPath);
    for (size_t i = 0; i < NUM_LANES; ++i) {
        config.lanes[i].videoSource = runfiles->Rlocation(config.lanes[i].videoSource);
    }

    std::array<Roi, NUM_LANES> laneRois;
    for (size_t i = 0; i < NUM_LANES; ++i) {
        laneRois[i] = config.lanes[i].roi;
    }

    constexpr uint32_t kFramePoolSize = 20;
    
    FramePool pool;
    if (!pool.init(kFramePoolSize)) return 1;
    
    AtomicFrameBuffer buffer;
    
    // Setup 4 workers
    std::vector<std::unique_ptr<StreamWorker>> workers;
    for (uint32_t i = 0; i < 4; ++i) {
        auto worker = std::make_unique<StreamWorker>();
        worker->initStream(config.lanes[i].videoSource, i, &pool, config.CapturePeriod);
        worker->start(buffer);
        workers.push_back(std::move(worker));
    }
    
    // PipelineManager
    auto backend = std::make_unique<YoloV8OIV7Backend>(config.modelPath);
    YoloV8OIV7Backend* backendPtr = backend.get(); 
    PipelineManager pipeline(std::move(backend), buffer, pool, laneRois);

    // Snapshot Publisher Integration
    auto sender = std::make_unique<MQSnapshotSender>();
    if (sender->open()) {
        pipeline.getPublisher().initSender(sender.get());
    } else {
        std::cerr << "Failed to open snapshot sender" << std::endl;
    }

    OpenCVLanesViewer viewer;
    viewer.init(config, backendPtr);

    bool running = true;
    auto nextPipelineRun = std::chrono::steady_clock::now();

    while (running) {
        nextPipelineRun += config.PipelinePeriod;

        // Pipeline executes one cycle
        pipeline.runOneCycle();

        // Visualize
        viewer.render(pipeline.analyzer());
        
        // Handle UI
        int key = cv::waitKey(30);
        if (key == 'q' || key == 27) running = false;
        
        std::this_thread::sleep_until(nextPipelineRun);
    }
    
    viewer.shutdown();
    for (auto& worker : workers) worker->stop();
    
    return 0;
}
