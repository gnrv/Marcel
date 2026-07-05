#pragma once

// Worker-side accounting for one slide's kBuffersPerSlide render buffers.
// FREE means ours to render into; a buffer handed out by acquire() stays
// unavailable (INFLIGHT in the socket, then HELD as the peer's front
// texture) until the peer sends BufferRelease. If nothing is FREE the
// worker simply skips the slide for a frame — the peer keeps displaying
// its front buffer, so starvation can never tear.

#include "Protocol.h"

#include <array>

namespace ipc {

class BufferRing {
public:
    static constexpr int kInvalid = -1;

    // Returns a FREE buffer index and marks it busy, or kInvalid.
    int acquire()
    {
        for (uint32_t i = 0; i < kBuffersPerSlide; ++i) {
            uint32_t b = (next_ + i) % kBuffersPerSlide;
            if (!busy_[b]) {
                busy_[b] = true;
                next_ = (b + 1) % kBuffersPerSlide;
                return static_cast<int>(b);
            }
        }
        return kInvalid;
    }

    // BufferRelease from the peer. Out-of-range or already-free indices are
    // ignored (stale messages must not corrupt the ring).
    void release(uint32_t index)
    {
        if (index < kBuffersPerSlide)
            busy_[index] = false;
    }

    void reset() { busy_ = {}; }

    uint32_t freeCount() const
    {
        uint32_t n = 0;
        for (bool b : busy_)
            n += !b;
        return n;
    }

private:
    std::array<bool, kBuffersPerSlide> busy_{};
    uint32_t next_ = 0;
};

} // namespace ipc
