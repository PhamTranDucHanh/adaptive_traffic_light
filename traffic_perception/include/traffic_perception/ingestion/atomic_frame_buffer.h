#ifndef INGESTION_ATOMIC_FRAME_BUFFER_H
#define INGESTION_ATOMIC_FRAME_BUFFER_H

#include <atomic>
#include <array>
#include <cstdint>

#include "traffic_perception/core/types.h"

namespace traffic_perception {

constexpr std::size_t NUM_LANES = 4;

class AtomicFrameBuffer {
public:
    // Takes ownership of frame
    bool publish(uint32_t laneId, Frame* frame);

    // Transfers ownership of returned frame to caller
    Frame* consume(uint32_t laneId);

private:
    std::array<std::atomic<Frame*>, NUM_LANES> buffers_{};
};

}  // namespace traffic_perception

#endif  // INGESTION_ATOMIC_FRAME_BUFFER_H
