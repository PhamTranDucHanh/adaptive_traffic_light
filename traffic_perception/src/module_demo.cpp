#include "traffic_perception/perception_module.h"
#include "traffic_perception/core/health_reporter.h"
#include "traffic_perception/pipeline/pipeline_node.h"
#include <iostream>

int main() {
    std::cout << "=== Traffic Perception Module Demo ===" << '\n';

    // 1. Instantiating HealthReporter to show setup/signals
    HealthReporter health;
    health.sendStartSignal();

    // 2. Initializing Orchestrator (PerceptionModule)
    traffic_perception::PerceptionModule perception;
    if (perception.initModule("config/app_config.json")) {
        std::cout << "Module initialized successfully!" << '\n';
    } else {
        std::cerr << "Failed to initialize module!" << '\n';
        return 1;
    }

    // 3. Simulating pipeline execution flow
    // Create FrameContext (containing cv::Mat, vehicles, etc.)
    FrameContext ctx;
    ctx.FrameId = 1001;
    ctx.LaneId = 1;
    ctx.Frame = cv::Mat::zeros(480, 640, CV_8UC3); // Empty 640x480 black image

    std::cout << "\n--- Simulating Inference Stage ---" << '\n';
    InferenceEngine engine;
    engine.executeInference(ctx);

    std::cout << "\n--- Simulating Pipeline Stage Variant Execution via PipelineNode ---" << '\n';
    TrafficAnalyzer analyzer;
    analyzer.ZoneId = 42;
    MultiLaneViewer viewer;
    viewer.WindowName = "MultiLane Viewer";
    analyzer.initViewer(&viewer);

    SnapshotPublisher publisher;
    publisher.BrokerUrl = "tcp://localhost:1883";
    TelemetryManager telemetry;
    telemetry.TotalCycles = 50;
    MQSnapshotSender mqSender;

    publisher.initTelemetry(&telemetry);
    publisher.initSender(&mqSender);

    PipelineNode node1;
    node1.Stage = analyzer;

    PipelineNode node2;
    node2.Stage = publisher;
    node1.Next = &node2;
    node2.Next = nullptr;

    // Traverse the pipeline using std::visit
    PipelineNode* current = &node1;
    while (current != nullptr) {
        std::visit([&ctx](auto&& stage) {
            using T = std::decay_t<decltype(stage)>;
            if constexpr (std::is_same_v<T, TrafficAnalyzer>) {
                std::cout << "[PipelineNode] Executing TrafficAnalyzer Stage" << '\n';
                stage.trackAndAnalyze(ctx);
            } else if constexpr (std::is_same_v<T, SnapshotPublisher>) {
                std::cout << "[PipelineNode] Executing SnapshotPublisher Stage" << '\n';
                stage.broadcastSnapshot(ctx);
            }
        }, current->Stage);
        current = current->Next;
    }

    // 4. Wrapping up
    std::cout << "\n--- Wrapping Up ---" << '\n';
    health.sendEndSignal();
    
    std::cout << "=== Demo Finished Successfully ===" << '\n';
    return 0;
}
