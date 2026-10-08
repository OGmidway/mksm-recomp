#pragma once
#include "../../../ps2xRuntime/src/lib/Kernel/Stubs/MPEG.h"
#include "mk-cri-video-ring.h"

void mk_recovered_00467d48_0x467d48(uint8_t*,R5900Context*,PS2Runtime*);
void mk_recovered_00468950_0x468950(uint8_t*,R5900Context*,PS2Runtime*);

namespace {
constexpr ps2_stubs::MpegPictureGuestLayout mkMpegLayout{0xc4,0xec,0xf8,0xf0,0xf4};
void mkMpegCreate(uint8_t* rdram,R5900Context* ctx,PS2Runtime* runtime) {
    ps2_stubs::initializeMpegPlaybackState(getRegU32(ctx,4),mkMpegLayout);
    mk_recovered_00467d48_0x467d48(rdram,ctx,runtime);
}
void mkMpegReset(uint8_t* rdram,R5900Context* ctx,PS2Runtime* runtime) {
    ps2_stubs::initializeMpegPlaybackState(getRegU32(ctx,4),mkMpegLayout);
    mk_recovered_00468950_0x468950(rdram,ctx,runtime);
}
void mkMpegGetPicture(uint8_t* rdram,R5900Context* ctx,PS2Runtime* runtime) {
    const uint32_t mpeg=getRegU32(ctx,4);
    const uint32_t ring=(mpeg&0x1fffffffu)+0x48u;
    const auto consumed=mkConsumeCriVideoBlocks(rdram,PS2_RAM_SIZE,ring,[&](uint32_t source,uint32_t size) {
        // Bound decoded-ahead memory; the original game still controls the
        // ring producer and requests pictures at its normal decode boundary.
        if(ps2_stubs::getMpegQueuedPictureCount(mpeg)>=2)return false;
        R5900Context feed=*ctx;
        SET_GPR_U32(&feed,5,source);SET_GPR_U32(&feed,6,size);
        ps2_stubs::sceMpegAddBs(rdram,&feed,runtime);
        return getRegU32(&feed,2)==size;
    });
    if(consumed) {
        auto read=[&](uint32_t off) {uint32_t v;std::memcpy(&v,rdram+ring+off,4);return v;};
        const uint32_t head=read(12);
        // Keep CRI's subsequent IPU stop/restore accounting consistent with
        // bytes actually accepted by the host decoder. No GIF/DMA fake data.
        auto& mem=runtime->memory();
        mem.writeIORegister(0x1000b400,5);
        mem.writeIORegister(0x1000b410,(read(0)&0x0fffffffu)+head*2048u);
        mem.writeIORegister(0x1000b420,0);
        mem.writeIORegister(0x1000b430,(read(4)&0x0fffffffu)+head*16u);
    }
    // Retail 0x44cdd8 finalizes/pads the producer ring before setting these
    // codec flags. A sequence-end byte or an empty ring alone is not EOF.
    auto read=[&](uint32_t address) {uint32_t v;std::memcpy(&v,rdram+address,4);return v;};
    const uint32_t base=mpeg&0x1fffffffu;
    if(base>=12 && read(base-12)==1 && read(base-8)==1 &&
       read(ring+16)==0 && read(ring+20)==0)
        ps2_stubs::finishMpegElementaryStream(mpeg,runtime);
    ctx->pc=getRegU32(ctx,31);
    ps2_stubs::sceMpegGetPicture(rdram,ctx,runtime);
}
}
