#ifndef INGESTION_ATOMIC_FRAME_BUFFER_H
#define INGESTION_ATOMIC_FRAME_BUFFER_H

#include <atomic>
#include <array>
#include <cstdint>

#include "traffic_perception/core/types.h"

namespace traffic_perception {

class AtomicFrameBuffer {
public:
    // Exchanges current frame with new frame. Returns old frame.
    // Does not take ownership of frame.
    Frame* exchange(uint32_t laneId, Frame* frame);

    // Atomically takes the frame out of the buffer, leaving nullptr.
    // Transfers ownership to caller.
    Frame* take(uint32_t laneId);

    // Returns a pointer to the frame without taking ownership.
    Frame* peek(uint32_t laneId) const;

private:
    std::array<std::atomic<Frame*>, NUM_LANES> buffers_{};
};

}  // namespace traffic_perception

#endif  // INGESTION_ATOMIC_FRAME_BUFFER_H
