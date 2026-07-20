#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
#include <opencv2/opencv.hpp>
#include "traffic_perception/core/frame_pool.h"
#include "traffic_perception/ingestion/atomic_frame_buffer.h"
#include "traffic_perception/ingestion/stream_worker.h"
#include "tools/cpp/runfiles/runfiles.h"

using namespace traffic_perception;
using bazel::tools::cpp::runfiles::Runfiles;

int main(int argc, char* argv[]) {
    std::string error;
    std::unique_ptr<Runfiles> runfiles(Runfiles::Create(argv[0], &error));
    if (!runfiles) {
        std::cerr << "Failed to create runfiles: " << error << std::endl;
        return 1;
    }
    
    std::string sourceUri = runfiles->Rlocation("traffic_perception/test/data/traffic.mp4");
    if (sourceUri.empty()) {
        std::cerr << "Failed to locate traffic.mp4" << std::endl;
        return 1;
    }
    std::cout << "Resolved sourceUri: " << sourceUri << std::endl;

    constexpr auto kAcquisitionPeriod = std::chrono::milliseconds(200);

    FramePool pool;
    if (!pool.init(10)) {
        std::cerr << "Failed to init pool" << std::endl;
        return 1;
    }
    AtomicFrameBuffer buffer;
    StreamWorker worker;
    
    if (!worker.initStream(sourceUri, 0, &pool, kAcquisitionPeriod)) {
        std::cerr << "Failed to init worker" << std::endl;
        return 1;
    }
    
    worker.start(buffer);
    std::cout << cv::getBuildInformation() << std::endl;
    
    cv::namedWindow("StreamWorker Test", cv::WINDOW_NORMAL);
    cv::resizeWindow("StreamWorker Test", 1280, 720);
    
    std::cout << "Displaying stream. Press 'q' or ESC to exit." << std::endl;
    
    bool running = true;
    auto lastFrameArrivalTime = std::chrono::steady_clock::now();
    double captureFps = 0.0;
    
    while (running) {
        // Keep UI responsive
        int key = cv::waitKey(1);
        if (key == 'q' || key == 27) { // 27 is ESC
            running = false;
            break;
        }

        Frame* frame = buffer.take(0);
        if (frame) {
            auto currentTime = std::chrono::steady_clock::now();
            auto delta = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - lastFrameArrivalTime);
            
            if (delta.count() > 0) {
                captureFps = 1000.0 / delta.count();
            }
            lastFrameArrivalTime = currentTime;

            if (!frame->Image.empty()) {
                cv::Mat displayFrame = frame->Image.clone();
                
                std::string info = "ID: " + std::to_string(frame->FrameId) + 
                                   " | Capture FPS: " + std::to_string(static_cast<int>(captureFps)) +
                                   " | Period: " + std::to_string(kAcquisitionPeriod.count()) + "ms";
                
                cv::putText(displayFrame, info, cv::Point(20, 40), 
                            cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);
                
                cv::imshow("StreamWorker Test", displayFrame);
            }
            pool.release(frame);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    
    worker.stop();
    cv::destroyAllWindows();
    
    std::cout << "Test exited cleanly." << std::endl;
    return 0;
}
