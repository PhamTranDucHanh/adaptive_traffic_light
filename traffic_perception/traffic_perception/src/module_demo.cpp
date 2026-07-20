#include "traffic_perception/perception_module.h"
#include "traffic_perception/core/health_reporter.h"
#include "traffic_perception/pipeline/pipeline_manager.h"
#include <iostream>

using namespace traffic_perception;

int main() {
    std::cout << "=== Traffic Perception Module Demo ===" << '\n';

    HealthReporter health;
    health.sendStartSignal();

    PerceptionModule perception;
    if (perception.initModule("config/app_config.json")) {
        std::cout << "Module initialized successfully!" << '\n';
    } else {
        std::cerr << "Failed to initialize module!" << '\n';
        return 1;
    }

    FrameContext ctx;
    ctx.FrameId = 1001;
    ctx.LaneId = 1;
    ctx.CapturedFrame = new Frame();
    ctx.CapturedFrame->FrameId = 1001;
    ctx.CapturedFrame->Image = cv::Mat::zeros(480, 640, CV_8UC3); 

    std::cout << "\n--- Simulating Inference Stage ---" << '\n';
    InferenceEngine engine;
    engine.executeInference(ctx);

    std::cout << "\n--- Simulating Pipeline Execution via PipelineManager ---" << '\n';
    PipelineManager pipeline;
    
    // Setup components
    pipeline.getViewer().WindowName = "MultiLane Viewer";
    pipeline.getAnalyzer().initViewer(&pipeline.getViewer());
    pipeline.getAnalyzer().ZoneId = 42;

    TelemetryManager telemetry;
    telemetry.TotalCycles = 50;
    MQSnapshotSender mqSender;

    pipeline.getPublisher().initTelemetry(&telemetry);
    pipeline.getPublisher().initSender(&mqSender);

    pipeline.process(ctx);

    std::cout << "\n--- Wrapping Up ---" << '\n';
    health.sendEndSignal();
    
    std::cout << "=== Demo Finished Successfully ===" << '\n';
    return 0;
}
