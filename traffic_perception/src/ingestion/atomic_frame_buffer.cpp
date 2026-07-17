#include "traffic_perception/ingestion/atomic_frame_buffer.h"

namespace traffic_perception {

bool AtomicFrameBuffer::publish(uint32_t laneId, Frame* frame) {
    if (laneId >= NUM_LANES) return false;
    Frame* old = buffers_[laneId].exchange(frame);
    if (old != nullptr) {
        delete old; // Replaced frame is owned by publisher now, so delete it
    }
    return true;
}

Frame* AtomicFrameBuffer::consume(uint32_t laneId) {
    if (laneId >= NUM_LANES) return nullptr;
    return buffers_[laneId].exchange(nullptr);
}

}  // namespace traffic_perception
