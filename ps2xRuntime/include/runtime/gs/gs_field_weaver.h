#pragma once
#include "runtime/gs/gs_types.h"
#include <algorithm>
#include <cstring>

// Retain alternate scanlines when a half-height interlaced field arrives.
// History belongs to the GS instance, not the transient VRAM snapshot backend.
class GSFieldWeaver {
    PresentationFrame history;
    uint64_t mode = 0;
public:
    void reset() { history = {}; mode = 0; }
    void apply(PresentationFrame& frame, const GSPresentationRequest& request) {
        if ((request.smode2 & 3u) != 3u || !frame) { reset(); return; }
        const uint64_t newMode = request.pmode;
        if (!history || history.width != frame.width || history.height != frame.height || mode != newMode) {
            history = frame;
            mode = newMode;
            return;
        }
        const unsigned parity = unsigned(request.vsyncTick & 1u);
        for (unsigned y = parity; y < frame.height; y += 2u)
            std::memcpy(history.pixels.data() + y * 640u * 4u,
                        frame.pixels.data() + y * 640u * 4u, frame.width * 4u);
        frame.pixels = history.pixels;
    }
};
