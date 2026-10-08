#include "../runtime/mk-cri-video-ring.h"
#include "../../../ps2xRuntime/src/lib/Kernel/Stubs/MPEG.h"

static bool checkCriVideoRing() {
    struct Case {uint32_t capacity,start,queued,accepted,partial,next,remaining;};
    const Case cases[]={
#include "cri_ring_cases.inc"
    };
    std::vector<uint8_t> ram(0x20000,0x5a);
    const uint32_t ring=0x1000,base=0x10000;
    auto put=[&](uint32_t off,uint32_t v){std::memcpy(ram.data()+ring+off,&v,4);};
    auto get=[&](uint32_t off){uint32_t v;std::memcpy(&v,ram.data()+ring+off,4);return v;};
    for(const auto& c:cases) {
        put(0,base);put(8,c.capacity);put(12,c.start);put(16,c.queued);
        put(20,c.partial);put(24,c.capacity*2048u);
        const auto before=ram;
        uint32_t calls=0;
        const auto consumed=mkConsumeCriVideoBlocks(ram.data(),uint32_t(ram.size()),ring,[&](uint32_t src,uint32_t size) {
            if(calls>=c.accepted)return false;
            if(src!=base+((c.start+calls)%c.capacity)*2048u || size!=2048u)
                throw std::runtime_error("Incorrect CRI block sequence");
            ++calls;return true;
        });
        if(consumed!=c.accepted || get(12)!=c.next || get(16)!=c.remaining || get(20)!=c.partial)return false;
        auto expected=before;
        std::memcpy(expected.data()+ring+12,&c.next,4);
        std::memcpy(expected.data()+ring+16,&c.remaining,4);
        if(ram!=expected)return false;
    }
    put(16,9);const auto before=ram;bool rejected=false;
    try {mkConsumeCriVideoBlocks(ram.data(),uint32_t(ram.size()),ring,[](uint32_t,uint32_t){return true;});}
    catch(const std::runtime_error&) {rejected=true;}
    return rejected && ram==before;
}

static bool checkMpegGuestLayout() {
    auto runtime=std::make_unique<PS2Runtime>();
    std::vector<uint8_t> ram(PS2_RAM_SIZE,0xcd);
    constexpr uint32_t mpeg=0x90000,inner=0x91000,image=0xa0000;
    std::memcpy(ram.data()+mpeg+0x40,&inner,4);
    ps2_stubs::initializeMpegPlaybackState(mpeg,{0xc4,0xec,0xf8,0xf0,0xf4});
    ps2_stubs::enqueueMpegDecodedFrameForTesting(mpeg);
    R5900Context ctx{};
    SET_GPR_U32(&ctx,4,mpeg);SET_GPR_U32(&ctx,5,image);SET_GPR_U32(&ctx,6,7);
    runtime->eeScheduler().reset(ram.data(),ctx);
    ps2_stubs::sceMpegGetPicture(ram.data(),&ctx,runtime.get());
    auto read=[&](uint32_t off){uint32_t v;std::memcpy(&v,ram.data()+inner+off,4);return v;};
    const bool ok=read(0xc4)==1 && read(0xec)==(image|0x20000000u) && read(0xf8)==7 &&
        read(0xf0)==0 && read(0xf4)==0 && read(0xb0)==0xcdcdcdcd && read(0xd8)==0xcdcdcdcd &&
        read(0xe4)==0xcdcdcdcd && ps2_stubs::getMpegQueuedPictureCount(mpeg)==0;
    ps2_stubs::finishMpegElementaryStream(mpeg);
    ps2_stubs::finishMpegElementaryStream(mpeg); // Idempotent producer notification.
    ps2_stubs::sceMpegIsEnd(ram.data(),&ctx,nullptr);
    const bool ended=getRegU32(&ctx,2)==1;
    constexpr uint32_t other=0x92000;
    ps2_stubs::initializeMpegPlaybackState(other,{});
    SET_GPR_U32(&ctx,4,other);
    ps2_stubs::sceMpegIsEnd(ram.data(),&ctx,nullptr);
    const bool isolated=getRegU32(&ctx,2)==0;
    ps2_stubs::initializeMpegPlaybackState(mpeg,{});
    SET_GPR_U32(&ctx,4,mpeg);
    ps2_stubs::sceMpegIsEnd(ram.data(),&ctx,nullptr);
    const bool reset=getRegU32(&ctx,2)==0;
    ps2_stubs::enqueueMpegDecodedFrameForTesting(mpeg);
    ps2_stubs::finishMpegElementaryStream(mpeg);
    ps2_stubs::sceMpegIsEnd(ram.data(),&ctx,nullptr);
    const bool retainsLastFrame=getRegU32(&ctx,2)==0 && ps2_stubs::getMpegQueuedPictureCount(mpeg)==1;
    ps2_stubs::resetMpegStubState();
    return ok && ended && isolated && reset && retainsLastFrame;
}
