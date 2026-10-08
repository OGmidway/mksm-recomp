#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_recompiled_functions.h"
#include "ps2_stubs.h"
#include "ps2_syscalls.h"
#include "../debug-retail-controls.h"
#include "../mk-mpeg-bridge.h"
#include <cstdlib>
#include <iostream>
#include <chrono>
void mk_recovered_0035f440_0x35f440(uint8_t*,R5900Context*,PS2Runtime*);
namespace {
// Read-only, bounded tracing of copies that overlap the live Havok allocator.
void traceMkHavokCopy(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    static unsigned reports=0;
    const uint32_t object=READ32(0x5b2010u)&0x1fffffffu;
    const uint32_t dst=GPR_U32(ctx,4)&0x1fffffffu;
    const uint32_t src=GPR_U32(ctx,5)&0x1fffffffu;
    const uint32_t size=GPR_U32(ctx,6),caller=GPR_U32(ctx,31);
    const bool valid=object && object<=PS2_RAM_SIZE-96u;
    const bool overlap=valid && uint64_t(dst)<uint64_t(object)+96u && uint64_t(dst)+size>object;
    const uint32_t before=valid?READ32(object+0x20u):0u;
    sub_0020FDE0_0x20fde0(rdram,ctx,runtime);
    const uint32_t after=valid?READ32(object+0x20u):0u;
    if(reports<16 && (overlap || before!=after)) {
        ++reports;
        std::cerr<<"[mk-havok-copy] caller="<<std::hex<<caller<<" dst="<<dst
                 <<" src="<<src<<" bytes="<<size<<" allocator="<<object
                 <<" table_before="<<before<<" table_after="<<after<<std::dec<<'\n';
    }
}
void traceMkLevelParse(uint8_t* rdram,R5900Context* ctx,PS2Runtime* runtime) {
    static unsigned records=0;
    const uint32_t object=GPR_U32(ctx,4)&0x1fffffffu;
    if(records++<8 && object && object<PS2_RAM_SIZE-32) {
        const uint32_t data=READ32(object+8)&0x1fffffffu;
        std::cerr<<"[mk-level-parse] request="<<std::hex<<object<<" caller="<<GPR_U32(ctx,31);
        for(unsigned i=0;i<32;i+=4)std::cerr<<" r"<<i<<":"<<READ32(object+i);
        std::cerr<<" header=";
        if(data && data<PS2_RAM_SIZE-64)
            for(unsigned i=0;i<64;i+=4)std::cerr<<" "<<READ32(data+i);
        std::cerr<<std::dec<<'\n';
    }
    mk_recovered_0035f440_0x35f440(rdram,ctx,runtime);
}
void traceMkMovieState(uint8_t* rdram,R5900Context* ctx,PS2Runtime* runtime,uint32_t object) {
    using Clock=std::chrono::steady_clock;
    static auto last=Clock::time_point{};static unsigned samples=0;
    const auto now=Clock::now();
    if(samples<60 && now-last>=std::chrono::seconds(2) && object<PS2_RAM_SIZE-0x2acu){
        last=now;++samples;
        auto dump=[&](uint32_t base,std::initializer_list<uint32_t> offsets){
            for(auto offset:offsets)std::cerr<<" "<<offset<<":"<<READ32(base+offset);
        };
        std::cerr<<"[mk-movie-state] object="<<std::hex<<object;
        dump(object,{4,0x18,0x3c,0x40,0x54,0x78,0x7c,0x84,0x1ac});
        const uint32_t name=READ32(object+0x1a4)&0x1fffffffu;
        std::cerr<<" file=";
        if(name)for(unsigned i=0;i<160 && name+i<PS2_RAM_SIZE && rdram[name+i];++i)
            std::cerr<<char(rdram[name+i]);
        const uint32_t codec=READ32(object+0x3c)&0x1fffffffu;
        if(codec && codec<PS2_RAM_SIZE-0x3700){
            std::cerr<<" codec="<<codec;
            dump(codec,{0x44,0x48,0xa48,0xfbc,0xfc0,0xfd8,0x1004,0x1008,0x100c,0x1010,0x1fc0,0x2004,0x2114,0x35a8,0x35ac});
            const uint32_t video=READ32(codec+0x1fc0)&0x1fffffffu;
            if(video && video<PS2_RAM_SIZE-0x1200){
                std::cerr<<" video="<<video;
                dump(video,{0x178,0x1128,0x112c,0x1130,0x1134,0x113c,0x1140,0x1144,0x1148,0x1198,0x11a0,0x11a4,0x11a8,0x11ac});
                for(unsigned i=0;i<4 && i<READ32(video+0x178);++i){
                    std::cerr<<" frame["<<i<<"]";
                    dump(video+0x180+i*0xf0,{0,0x14,0x18,0x58,0x74});
                }
            }
        }
        std::cerr<<std::dec<<'\n';
    }
}
void traceMkMovieWorker(uint8_t* rdram,R5900Context* ctx,PS2Runtime* runtime) {
    static unsigned samples=0;
    if(samples++<4||samples==1000)std::cerr<<"[mk-movie-worker] active="<<READ32(0x55e1ec)
      <<" mode="<<READ32(0x55e200)<<" lock="<<READ32(0x55e248)<<" stop="<<READ32(0x55e214)<<'\n';
    sub_0043FAE8_0x43fae8(rdram,ctx,runtime);
}
void traceMkCriExecute(uint8_t* rdram,R5900Context* ctx,PS2Runtime* runtime) {
    traceMkMovieState(rdram,ctx,runtime,0x55e25c);
    static unsigned samples=0;const auto p=GPR_U32(ctx,4)&0x1fffffffu;
    if((samples++<4||samples==1000)&&p<PS2_RAM_SIZE-0x2008)std::cerr<<"[mk-cri-execute] object="<<std::hex<<p
      <<" status="<<READ32(p+0x48)<<" active="<<READ32(p+0x44)<<std::dec<<'\n';
    sub_0044D7C8_0x44d7c8(rdram,ctx,runtime);
}
void traceMkMoviePump(uint8_t* rdram,R5900Context* ctx,PS2Runtime* runtime) {
    traceMkMovieState(rdram,ctx,runtime,GPR_U32(ctx,4)&0x1fffffffu);
    sub_0043F450_0x43f450(rdram,ctx,runtime);
}
void traceMkMovieOpen(uint8_t* rdram,R5900Context* ctx,PS2Runtime* runtime) {
    static unsigned samples=0;const uint32_t object=GPR_U32(ctx,4)&0x1fffffffu;
    if(samples++<4&&object<PS2_RAM_SIZE-0x2acu)
        std::cerr<<"[mk-movie-open] object="<<std::hex<<object<<" stream="<<READ32(object+0x40)
                 <<" filename="<<READ32(object+0x1a4)<<std::dec<<'\n';
    sub_0043F678_0x43f678(rdram,ctx,runtime);
}
void traceMkMovieDecode(uint8_t* rdram,R5900Context* ctx,PS2Runtime* runtime) {
    static unsigned samples=0;
    if(samples++<4)std::cerr<<"[mk-movie-decode] object="<<std::hex<<GPR_U32(ctx,7)<<std::dec<<'\n';
    sub_0044CF60_0x44cf60(rdram,ctx,runtime);
}
void traceMkLoadStatus(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    static unsigned samples = 0;
    if (samples++ < 6) {
        std::cerr << "[mk-load] request=" << std::hex << GPR_U32(ctx,4)
                  << " active=" << READ32(0x5504d0u) << " id=" << READ32(0x5504d4u)
                  << " status=" << READ32(0x5504d8u) << " buffer=" << READ32(0x531310u) << std::dec << std::endl;
        uint32_t object=READ32(0x5504d0u);
        if(object && object < 0x01ffff00u) {
            std::cerr << "[mk-load-object]";
            for(unsigned i=0;i<0x60;i+=4) std::cerr << " " << std::hex << READ32(object+i);
            std::cerr << std::dec << std::endl;
        }
    }
    sub_004126D0_0x4126d0(rdram,ctx,runtime);
}
void traceMkError(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    static unsigned errors=0;
    if(errors++ < 24) {
        uint32_t address=GPR_U32(ctx,4)&0x1fffffffu;
        std::cerr << "[mk-error] ";
        for(unsigned i=0;i<256 && address+i<0x2000000u && rdram[address+i];++i) std::cerr << char(rdram[address+i]);
        std::cerr << " args=" << std::hex << GPR_U32(ctx,5) << "," << GPR_U32(ctx,6) << "," << GPR_U32(ctx,7) << std::dec << std::endl;
    }
    sub_0042D298_0x42d298(rdram,ctx,runtime);
}
// sceCdLayerSearchFile uses the standard sceCdlFILE layout plus a flags word.
// This extracted single-layer disc uses the runtime's existing file/LBN registry.
void mkLayerSearch(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    if (GPR_U32(ctx,6) != 0) {
        sub_00385CE0_0x385ce0(rdram,ctx,runtime);
        return;
    }
    const uint32_t result = GPR_U32(ctx,4);
    ps2_stubs::sceCdSearchFile(rdram,ctx,runtime);
    if (GPR_U32(ctx,2)) WRITE32(result + 32u, 0u);
}
void traceMkReadStatus(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    static unsigned samples=0;
    if (samples++ < 4 || samples == 1000) {
        const uint32_t object=READ32(0x4fadecu);
        std::cerr << "[mk-read] object=" << std::hex << object << " state=" << READ32(0x4ef7e0u)
                  << " requested=" << READ32(0x678440u) << " completed=" << READ32(0x678444u);
        if(object && object<0x1ffff00u) for(unsigned i=0;i<0x80;i+=4) std::cerr << " " << READ32(object+i);
        std::cerr << std::dec << std::endl;
        for(uint32_t base : {0x53ce10u,0x548c50u,0x53d4c8u,0x53d504u}) {
            std::cerr << "[mk-read-detail] " << std::hex << base;
            for(unsigned i=0;i<0x50;i+=4) std::cerr << " " << READ32(base+i);
            std::cerr << std::dec << std::endl;
        }
    }
    sub_0026F6E0_0x26f6e0(rdram,ctx,runtime);
}
void traceMkCdRead(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    static unsigned count=0;
    if(count++<8) std::cerr << "[mk-sector-read] lsn=" << std::hex << GPR_U32(ctx,4) << " sectors=" << GPR_U32(ctx,5) << " dest=" << GPR_U32(ctx,6) << std::dec << std::endl;
    const uint32_t dest=GPR_U32(ctx,6)&0x1fffffffu;
    const uint64_t bytes=uint64_t(GPR_U32(ctx,5))*2048u;
    ps2_stubs::sceCdRead(rdram,ctx,runtime);
    if(count<=8 && dest<0x2000000u && bytes<=0x2000000u-dest) {
        uint32_t hash=2166136261u;
        for(uint64_t i=0;i<bytes;++i) hash=(hash^rdram[dest+i])*16777619u;
        std::cerr << "[mk-sector-result] result=" << GPR_U32(ctx,2) << " bytes=" << bytes << " fnv1a=" << std::hex << hash << std::dec << std::endl;
    }
}
void traceMkCreateThread(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    uint32_t p=GPR_U32(ctx,4)&0x1fffffffu;
    if(p<0x1ffffdcu) std::cerr << "[mk-thread-create] entry=" << std::hex << READ32(p+4) << " stack=" << READ32(p+8) << " size=" << READ32(p+12) << " priority=" << READ32(p+20) << std::dec << std::endl;
    sub_0047FC60_0x47fc60(rdram,ctx,runtime);
    std::cerr << "[mk-thread-create] result=" << int32_t(GPR_U32(ctx,2)) << std::endl;
}
// InitThread's SDK implementation lowers the application thread to priority 1.
// The runtime handles the kernel helper's scheduling services directly.
void mkInitThread(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    const uint64_t a0=GPR_U64(ctx,4), a1=GPR_U64(ctx,5);
    ps2_syscalls::GetThreadId(rdram,ctx,runtime);
    const uint32_t id=GPR_U32(ctx,2);
    SET_GPR_U32(ctx,4,id);
    SET_GPR_U32(ctx,5,1);
    ps2_syscalls::ChangeThreadPriority(rdram,ctx,runtime);
    SET_GPR_U64(ctx,4,a0);
    SET_GPR_U64(ctx,5,a1);
    ps2_syscalls::InitThread(rdram,ctx,runtime);
}
void traceMkReadRequest(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    static unsigned samples=0;
    if(samples++<12) std::cerr << "[mk-read-request] caller=" << std::hex << GPR_U32(ctx,31) << " file=" << GPR_U32(ctx,4) << " sectors=" << GPR_U32(ctx,5) << " buffer=" << GPR_U32(ctx,6) << " sp=" << GPR_U32(ctx,29) << std::dec << std::endl;
    sub_004135F8_0x4135f8(rdram,ctx,runtime);
}
void traceMkArchiveRequest(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    static unsigned count=0;
    if(count++<12) std::cerr << "[mk-archive-request] caller=" << std::hex << GPR_U32(ctx,31) << " offset=" << GPR_U32(ctx,5) << " bytes=" << GPR_U32(ctx,6) << " buffer=" << GPR_U32(ctx,7) << std::dec << std::endl;
    sub_00370A70_0x370a70(rdram,ctx,runtime);
}
void traceMkAllocation(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    static unsigned count=0;
    if(count++<32 || (GPR_U32(ctx,31)==0x378bb4u && count<160)) {
        const uint32_t flags=GPR_U32(ctx,4), heap=0x64e998u+(flags&0x7fu)*0x68u;
        std::cerr << "[mk-alloc] caller=" << std::hex << GPR_U32(ctx,31)
                  << " flags=" << flags << " size=" << GPR_U32(ctx,5) << " heap=" << heap;
        for(unsigned i=0;i<0x68;i+=4) std::cerr << " " << READ32(heap+i);
        std::cerr << std::dec << std::endl;
    }
    sub_0020F058_0x20f058(rdram,ctx,runtime);
}
void traceMkAsyncBuffer(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    static unsigned count=0;
    if(count++<12) {
        const uint32_t request=READ32(0x755ac8u);
        std::cerr << "[mk-async-buffer] caller=" << std::hex << GPR_U32(ctx,31)
                  << " buffer=" << GPR_U32(ctx,4) << " request=" << request;
        if(request && request < 0x1ffffccu)
            for(unsigned i=0;i<0x34;i+=4) std::cerr << " " << READ32(request+i);
        std::cerr << std::dec << std::endl;
    }
    sub_002123A0_0x2123a0(rdram,ctx,runtime);
}
void traceMkHeapProbeReturn(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    static unsigned count=0;
    if(count++<6 || GPR_U32(ctx,2)!=0) {
        std::cerr << "[mk-heap-probe] pc=" << std::hex << ctx->pc
                  << " result=" << GPR_U32(ctx,2) << " size-s1=" << GPR_U32(ctx,17)
                  << " brk=" << READ32(0x563fd4u) << " limit=" << runtime->guestHeapLimit()
                  << std::dec << std::endl;
    }
    sub_00212F70_0x212f70(rdram,ctx,runtime);
}
void traceMkHeapInit(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    std::cerr << "[mk-heap-init] base=" << std::hex << GPR_U32(ctx,4)
              << " end=" << GPR_U32(ctx,5) << std::dec << std::endl;
    sub_0020ECD0_0x20ecd0(rdram,ctx,runtime);
}
void traceMkSbrkLimit(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    static unsigned count=0;
    static bool failed=false;
    const bool fits=GPR_U64(ctx,16)<=runtime->guestHeapLimit();
    const bool transition=failed && fits;
    failed=!fits;
    if(count++<20 || transition) {
        std::cerr << "[mk-sbrk-limit] ra=" << std::hex << GPR_U32(ctx,31)
                  << " increment=" << GPR_U64(ctx,4) << " candidate=" << GPR_U64(ctx,16)
                  << " brk=" << READ32(0x563fd4u) << " limit=" << runtime->guestHeapLimit()
                  << " top=" << READ32(0x563b68u) << std::dec << std::endl;
    }
    sub_0047FE40_0x47fe40(rdram,ctx,runtime);
}
void traceMkMalloc(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    const uint32_t caller=GPR_U32(ctx,31), size=GPR_U32(ctx,4), saved=GPR_U32(ctx,17);
    static uint32_t previous=0; static unsigned gaps=0;
    if(caller==0x2130a0u) {
        if(previous && size!=previous-4 && gaps++<8)
            std::cerr << "[mk-probe-gap] previous=" << std::hex << previous << " next=" << size
                      << " sp=" << GPR_U32(ctx,29) << " s1=" << saved << std::dec << std::endl;
        previous=size;
    }
    sub_00476988_0x476988(rdram,ctx,runtime);
    static unsigned samples=0;
    if(caller==0x2130a0u && ctx->pc==caller &&
       (samples<6 || GPR_U32(ctx,2)!=0 || saved!=GPR_U32(ctx,17))) {
        if(samples++<24) std::cerr << "[mk-malloc-return] size=" << std::hex << size
            << " value=" << GPR_U32(ctx,2) << " s1-before=" << saved
            << " s1-after=" << GPR_U32(ctx,17) << " brk=" << READ32(0x563fd4u)
            << std::dec << std::endl;
    }
}
void installMkLoadTrace(PS2Runtime& runtime) {
    if(std::getenv("PS2X_BOOT_TRACE")) {
        runtime.replaceFunction(0x35f440u,traceMkLevelParse);
        runtime.replaceFunction(0x43fae8u,traceMkMovieWorker);
        runtime.replaceFunction(0x44d7c8u,traceMkCriExecute);
        runtime.replaceFunction(0x43f450u,traceMkMoviePump);
        runtime.replaceFunction(0x43f678u,traceMkMovieOpen);
        runtime.replaceFunction(0x44cf60u,traceMkMovieDecode);
    }
    runtime.replaceFunction(0x480af0u,mkInitThread);
    runtime.replaceFunction(0x467d48u,mkMpegCreate);
    runtime.replaceFunction(0x468950u,mkMpegReset);
    runtime.replaceFunction(0x4688b0u,mkMpegGetPicture);
    runtime.replaceFunction(0x00385ce0u,mkLayerSearch);
    const char* skipIntro=std::getenv("MKSM_DEBUG_SKIP_INTRO");
    if(skipIntro && std::strcmp(skipIntro,"1")==0) {
        runtime.replaceFunction(0x1d32e0u,mkDebugSkipIntro);
        std::cerr << "[mk-debug] Retail intro opt-out enabled; movie decoding remains unverified\n";
    }
    if(std::getenv("MKSM_TRACE_HAVOK_COPY")) runtime.replaceFunction(0x20fde0u,traceMkHavokCopy);
    if(std::getenv("PS2X_BOOT_TRACE")) { runtime.replaceFunction(0x476988u,traceMkMalloc); runtime.replaceFunction(0x47fe40u,traceMkSbrkLimit); runtime.replaceFunction(0x213060u,traceMkHeapProbeReturn); runtime.replaceFunction(0x2130a0u,traceMkHeapProbeReturn); runtime.replaceFunction(0x2130f8u,traceMkHeapProbeReturn); runtime.replaceFunction(0x20ecd0u,traceMkHeapInit); runtime.replaceFunction(0x20f058u,traceMkAllocation); runtime.replaceFunction(0x2123a0u,traceMkAsyncBuffer); runtime.replaceFunction(0x370a70u,traceMkArchiveRequest); runtime.replaceFunction(0x4135f8u,traceMkReadRequest); runtime.replaceFunction(0x26f6e0u,traceMkReadStatus); runtime.replaceFunction(0x467940u,traceMkCdRead); runtime.replaceFunction(0x47fc60u,traceMkCreateThread); runtime.replaceFunction(0x004126d0u,traceMkLoadStatus); runtime.replaceFunction(0x0042d298u,traceMkError); }
}
}
PS2_REGISTER_GAME_OVERRIDE("Shaolin Monks loading diagnostics", "SLUS_210.87", 0x11c070u, 0x1a37a67cu, installMkLoadTrace)

#include "../pr244-hooks.inc"
