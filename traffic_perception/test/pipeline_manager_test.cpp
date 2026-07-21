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
#include "traffic_perception/inference/yolov8_backend.h"
#include "traffic_perception/pipeline/pipeline_manager.h"
#include "traffic_perception/pipeline/snapshot_publisher.h"
#include "traffic_perception/io/snapshot_sender.h"
#include "tools/cpp/runfiles/runfiles.h"

using namespace traffic_perception;
using bazel::tools::cpp::runfiles::Runfiles;

int main(int argc, char* argv[]) {
    // Bazel Runfiles setup
    std::string error;
    std::unique_ptr<Runfiles> runfiles(Runfiles::Create(argv[0], &error));
    if (!runfiles) return 1;
    
    std::string sourceUri = runfiles->Rlocation("traffic_perception/test/data/traffic.mp4");
    if (sourceUri.empty()) return 1;

    // Load configuration
    ConfigManager configManager;
    configManager.loadConfig();
    AppConfig config = configManager.getConfig();

    constexpr uint32_t kFramePoolSize = 20;
    constexpr auto kCapturePeriod = std::chrono::milliseconds(200);
    constexpr auto kPipelinePeriod = std::chrono::milliseconds(100);

    FramePool pool;
    if (!pool.init(kFramePoolSize)) return 1;
    
    AtomicFrameBuffer buffer;
    
    // Setup 4 workers
    std::vector<std::unique_ptr<StreamWorker>> workers;
    for (uint32_t i = 0; i < 4; ++i) {
        auto worker = std::make_unique<StreamWorker>();
        worker->initStream(sourceUri, i, &pool, kCapturePeriod);
        worker->start(buffer);
        workers.push_back(std::move(worker));
    }
    
    // PipelineManager
    auto backend = std::make_unique<YOLOv8Backend>(runfiles->Rlocation("traffic_perception/test/data/yolov8n.onnx"));
    YOLOv8Backend* backendPtr = backend.get(); 
    PipelineManager pipeline(std::move(backend), buffer, pool, configManager);

    // Snapshot Publisher Integration
    auto sender = std::make_unique<MQSnapshotSender>();
    if (sender->open()) {
        pipeline.getPublisher().initSender(sender.get());
    } else {
        std::cerr << "Failed to open snapshot sender" << std::endl;
    }

    cv::namedWindow("Pipeline Integration", cv::WINDOW_AUTOSIZE);

    std::array<cv::Mat, 4> lastRenderedFrames;
    for (auto& frame : lastRenderedFrames) frame = cv::Mat();

    bool running = true;
    auto nextPipelineRun = std::chrono::steady_clock::now();

    while (running) {
        nextPipelineRun += kPipelinePeriod;

        // Pipeline executes one cycle
        pipeline.runOneCycle();

        // Get latest frames
        auto frames = pipeline.analyzer().takeRenderFrames();

        std::vector<cv::Mat> canvasLanes(4);
        for (uint32_t i = 0; i < 4; ++i) {
            if (frames[i] && !frames[i]->Image.empty()) {
                // Get latest context for detections
                const auto& ctx = pipeline.analyzer().latestContext(i);
                
                // Draw detections onto frame using the backend
                backendPtr->draw(frames[i]->Image, ctx.Detections);
                
                // Draw ROI
                const auto& roi = config.laneRois[i];
                std::vector<std::vector<cv::Point>> contours = {roi.Points};
                cv::polylines(frames[i]->Image, contours, true, cv::Scalar(0, 255, 0), 2);
                cv::putText(frames[i]->Image, roi.LaneId, roi.Points[0], 
                            cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);

                // Resize/copy into cache
                cv::resize(frames[i]->Image, lastRenderedFrames[i], cv::Size(320, 240));
                pipeline.analyzer().releaseFrame(frames[i]);
            }

            // Compose output
            if (!lastRenderedFrames[i].empty()) {
                canvasLanes[i] = lastRenderedFrames[i];
            } else {
                canvasLanes[i] = cv::Mat(240, 320, CV_8UC3, cv::Scalar(0, 0, 0));
                cv::putText(canvasLanes[i], "Waiting...", cv::Point(100, 120),
                            cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 255), 2);
            }
        }
        
        cv::Mat top, bottom, canvas;
        cv::hconcat(canvasLanes[0], canvasLanes[1], top);
        cv::hconcat(canvasLanes[2], canvasLanes[3], bottom);
        cv::vconcat(top, bottom, canvas);
        
        cv::imshow("Pipeline Integration", canvas);
        
        // Handle UI
        int key = cv::waitKey(30);
        if (key == 'q' || key == 27) running = false;
        
        std::this_thread::sleep_until(nextPipelineRun);
    }
    
    for (auto& worker : workers) worker->stop();
    
    cv::destroyAllWindows();
    return 0;
}
