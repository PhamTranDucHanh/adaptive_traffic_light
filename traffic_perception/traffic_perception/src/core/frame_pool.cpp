#include "traffic_perception/core/frame_pool.h"

namespace traffic_perception {

FramePool::~FramePool() {
    for (auto* frame : pool_) {
        delete frame;
    }
}

bool FramePool::init(uint32_t poolSize) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (uint32_t i = 0; i < poolSize; ++i) {
        auto* frame = new Frame();
        pool_.push_back(frame);
        free_list_.push_back(frame);
    }
    return true;
}

Frame* FramePool::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (free_list_.empty()) {
        return nullptr;
    }
    Frame* frame = free_list_.back();
    free_list_.pop_back();
    return frame;
}

void FramePool::release(Frame* frame) {
    if (!frame) return;

    // Reset metadata
    frame->FrameId = 0;
    frame->Image = cv::Mat(); // Clear image buffer

    std::lock_guard<std::mutex> lock(mutex_);
    free_list_.push_back(frame);
}

uint32_t FramePool::available() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    return static_cast<uint32_t>(free_list_.size());
}

}  // namespace traffic_perception
