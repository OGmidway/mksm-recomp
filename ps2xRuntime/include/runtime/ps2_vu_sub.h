#pragma once
#include "ps2_runtime.h"
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

// Synchronous macro-mode VSUB. Micro-mode scheduling remains in VU1Interpreter.
// Keep VF0 immutable even when the instruction is used only for MAC flags.
inline void ps2VuSub(R5900Context* ctx, uint32_t instruction) {
    const unsigned mask=(instruction>>21)&15u,fd=(instruction>>6)&31u;
    const unsigned fs=(instruction>>11)&31u,ft=(instruction>>16)&31u,op=instruction&63u;
    std::array<float,4> left,right,result;
    std::memcpy(left.data(),&ctx->vu0_vf[fs],16);
    std::memcpy(right.data(),&ctx->vu0_vf[ft],16);
    std::memcpy(result.data(),&ctx->vu0_vf[fd],16);
    const auto normalized=[](float value) {
        uint32_t bits;std::memcpy(&bits,&value,4);
        const unsigned exponent=(bits>>23)&255u;
        if (!exponent) bits&=0x80000000u;
        else if (exponent==255u) bits=(bits&0x80000000u)|0x7f7fffffu;
        std::memcpy(&value,&bits,4);return double(value);
    };
    uint32_t mac=0,status=0;
    for (unsigned component=0;component<4;++component) {
        const unsigned lane=8u>>component;
        if (!(mask&lane)) continue;
        const float rhs=op==0x24u?ctx->vu0_q:op==0x26u?ctx->vu0_i:
                        (op>=4u&&op<=7u)?right[op&3u]:right[component];
        const double exact=normalized(left[component])-normalized(rhs);
        const bool negative=std::signbit(exact);
        const double magnitude=std::fabs(exact);
        uint32_t flags=negative?2u:0u,bits=negative?0x80000000u:0u;
        if (magnitude==0.0) flags|=1u;
        else if (magnitude>std::numeric_limits<float>::max()) {flags|=8u;bits|=0x7f7fffffu;}
        else if (magnitude<std::numeric_limits<float>::min()) flags|=5u;
        else {const float value=static_cast<float>(exact);std::memcpy(&bits,&value,4);}
        std::memcpy(&result[component],&bits,4);
        for (unsigned bit=0;bit<4;++bit) if (flags&(1u<<bit)) mac|=lane<<(bit*4u);
        status|=flags;
    }
    if (fd) std::memcpy(&ctx->vu0_vf[fd],result.data(),16);
    ctx->vu0_mac_flags=mac;
    ctx->vu0_status=static_cast<uint16_t>((ctx->vu0_status&0xff0u)|status|(status<<6u));
}
