#include "traffic_perception/ingestion/atomic_frame_buffer.h"

namespace traffic_perception {

Frame* AtomicFrameBuffer::exchange(uint32_t laneId, Frame* frame) {
    if (laneId >= NUM_LANES) return nullptr;
    return buffers_[laneId].exchange(frame);
}

Frame* AtomicFrameBuffer::take(uint32_t laneId) {
    if (laneId >= NUM_LANES) return nullptr;
    // Atomically exchange with nullptr to ensure that the consumer takes exclusive
    // ownership of the frame. Any subsequent take() calls will return nullptr until
    // a new frame is published via exchange(), enforcing single-consumer semantics.
    return buffers_[laneId].exchange(nullptr);
}

Frame* AtomicFrameBuffer::peek(uint32_t laneId) const {
    if (laneId >= NUM_LANES) return nullptr;
    return buffers_[laneId].load();
}

}  // namespace traffic_perception
