#include "traffic_perception/core/frame_pool.h"

#include <sys/mman.h>
#include <cstring>
#include <unistd.h>
#include <iostream>

#include "score/mw/log/logging.h"

namespace traffic_perception {

FramePool::~FramePool() {
    for (auto* frame : pool_) {
        if (!frame->Image.empty() && frame->Image.isContinuous()) {
            munlock(frame->Image.data, frame->Image.total() * frame->Image.elemSize());
        }
        delete frame;
    }
}

bool FramePool::init(uint32_t poolSize, int width, int height) {
    std::lock_guard<std::mutex> lock(mutex_);
    const size_t pageSize = sysconf(_SC_PAGESIZE);

    for (uint32_t i = 0; i < poolSize; ++i) {
        auto* frame = new Frame();
        if (width > 0 && height > 0) {
            frame->Image.create(height, width, CV_8UC3);
            size_t totalBytes = frame->Image.total() * frame->Image.elemSize();

            // Prefault pages to avoid page faults during runtime
            volatile uint8_t* ptr = frame->Image.data;
            for (size_t offset = 0; offset < totalBytes; offset += pageSize) {
                ptr[offset] = 0;
            }
            if (totalBytes > 0) {
                ptr[totalBytes - 1] = 0;
            }

            // Lock memory pages in RAM
            if (mlock(frame->Image.data, totalBytes) != 0) {
                score::mw::log::LogWarn()
                    << "[FramePool] mlock failed for frame " << i
                    << " (bytes=" << totalBytes << ")";
            }
        }
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

    // Reset metadata while preserving preallocated buffer memory
    frame->FrameId = 0;
    frame->timeline.entries.clear();

    std::lock_guard<std::mutex> lock(mutex_);
    free_list_.push_back(frame);
}

}  // namespace traffic_perception
