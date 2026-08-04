#ifndef CORE_FRAME_POOL_H
#define CORE_FRAME_POOL_H

#include <vector>
#include <mutex>
#include <cstdint>
#include "traffic_perception/core/types.h"

namespace traffic_perception {

class FramePool {
public:
    FramePool() = default;
    ~FramePool();

    bool init(uint32_t poolSize, int width = 0, int height = 0);
    Frame* acquire();
    void release(Frame* frame);

private:
    std::mutex mutex_;
    std::vector<Frame*> pool_;
    std::vector<Frame*> free_list_;
};

}  // namespace traffic_perception

#endif  // CORE_FRAME_POOL_H
