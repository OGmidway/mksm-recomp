#pragma once
#include "ps2_runtime.h"
#include <cstdint>
#include <cstring>

// Macro CLIP: six ordered plane bits, then four results of history in 24 bits.
// Compare raw magnitudes so signed W and denormal operands follow VU semantics.
// Micro-mode latency remains managed by VU1Interpreter.
inline void ps2VuClip(R5900Context* ctx, uint32_t instruction) {
    uint32_t source[4], threshold[4];
    std::memcpy(source, &ctx->vu0_vf[(instruction >> 11) & 31u], 16);
    std::memcpy(threshold, &ctx->vu0_vf[(instruction >> 16) & 31u], 16);
    const uint32_t w = threshold[3];
    const uint32_t limit = (w & 0x7f800000u) ? w & 0x7fffffffu : 0x007fffffu;
    uint32_t flags = 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        if ((source[axis] & 0x7fffffffu) > limit)
            flags |= 1u << (axis * 2u + (source[axis] >> 31));
    }
    ctx->vu0_clip_flags = ((ctx->vu0_clip_flags << 6) | flags) & 0x00ffffffu;
}
