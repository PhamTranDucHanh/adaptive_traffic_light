#include "traffic_perception/viewer/opencv_lanes_viewer.h"

#include "score/mw/log/logger.h"
#include <chrono>

namespace {
    inline auto& getViewerBenchmarkLogger() {
        static auto& logger = score::mw::log::CreateLogger(
            "VIEW",
            "Traffic Perception Viewer");
        return logger;
    }
}

namespace traffic_perception {

bool OpenCVLanesViewer::init(const AppConfig& config, IModelBackend* backend) {
    config_ = config;
    backend_ = backend;
    windowName_ = "Pipeline Integration";
    cv::namedWindow(windowName_, cv::WINDOW_AUTOSIZE);
    for (auto& frame : lastRenderedFrames_) frame = cv::Mat();
    return true;
}

void OpenCVLanesViewer::render(Analyzer& analyzer) {
    auto render_start = std::chrono::steady_clock::now().time_since_epoch().count();
    auto frames = analyzer.takeRenderFrames();

    std::vector<cv::Mat> canvasLanes(4);
    for (uint32_t i = 0; i < 4; ++i) {
        if (frames[i] && !frames[i]->Image.empty()) {
            const auto& ctx = analyzer.latestContext(i);
            
            // Draw detections
            if (backend_) {
                InferenceResult result;
                result.Detections = ctx.Detections;
                backend_->draw(frames[i]->Image, result);
            }
            
            // Draw ROI
            const auto& roi = config_.lanes[i].roi;
            std::vector<std::vector<cv::Point>> contours = {roi.Points};
            cv::polylines(frames[i]->Image, contours, true, cv::Scalar(0, 255, 0), 2);
            cv::putText(frames[i]->Image, roi.LaneId, roi.Points[0], 
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);

            // Resize/copy into cache
            cv::resize(frames[i]->Image, lastRenderedFrames_[i], cv::Size(320, 240));
            analyzer.releaseFrame(frames[i]);
        }

        // Compose output
        if (!lastRenderedFrames_[i].empty()) {
            canvasLanes[i] = lastRenderedFrames_[i];
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
    
    cv::imshow(windowName_, canvas);
    // Benchmark render timestamps
    auto render_end = std::chrono::steady_clock::now().time_since_epoch().count();
    getViewerBenchmarkLogger().LogInfo()
        << "RenderBegin=" << render_start
        << " RenderEnd=" << render_end;
}

void OpenCVLanesViewer::shutdown() {
    cv::destroyWindow(windowName_);
}

}  // namespace traffic_perception
