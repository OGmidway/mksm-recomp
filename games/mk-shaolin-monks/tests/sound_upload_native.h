#include "../../../ps2xRuntime/src/lib/ps2_iop_transport.h"
#include "../../../ps2xRuntime/src/lib/Kernel/Stubs/Audio.h"
#include "../../../ps2xRuntime/src/lib/Kernel/Stubs/SIF.h"

static bool checkSoundMemoryUploads()
{
    auto rt=std::make_unique<PS2Runtime>();auto other=std::make_unique<PS2Runtime>();
    std::vector<uint8_t> ram(PS2_RAM_SIZE);R5900Context ctx{};
    SET_GPR_U32(&ctx,29,0x1e00000);
    // Garbage stack values must not replace valid zero n32 arguments.
    const uint32_t garbage=0xffffffff;
    for(unsigned offset:{0x10u,0x14u,0x18u})std::memcpy(ram.data()+0x1e00000+offset,&garbage,4);
    ps2_stubs::sceSdRemoteInit(ram.data(),&ctx,rt.get());
    auto call=[&](uint32_t cmd,uint32_t core,uint32_t mode,uint32_t src,uint32_t dst,uint32_t bytes){
        SET_GPR_U32(&ctx,5,cmd);SET_GPR_U32(&ctx,6,core);SET_GPR_U32(&ctx,7,mode);
        SET_GPR_U32(&ctx,8,src);SET_GPR_U32(&ctx,9,dst);SET_GPR_U32(&ctx,10,bytes);
        ps2_stubs::sceSdRemote(ram.data(),&ctx,rt.get());return getRegU32(&ctx,2);
    };
    call(0x80e0,1,0x13,0x12340,0x3000,0x12740);
    if(call(0x8100,1,0,0,0,0)!=0x12b40)return false;
    const auto iop=ps2_stubs::allocateSifIopMemory(128);
    if(!iop)return false;
    std::array<uint8_t,64> source{},result{};
    for(unsigned i=0;i<64;++i)source[i]=uint8_t(i*3+1);
    if(!ps2_stubs::writeSifIopHeap(iop,source.data(),64))return false;
    if(call(0x80d0,1,0,iop,0,64)!=64 || call(0x80f0,1,1,0,0,0)!=1)return false;
    if(!PS2IopTransport::readSoundMemory(rt.get(),0,result.data(),64)||result!=source)return false;
    if(PS2IopTransport::readSoundMemory(other.get(),0,result.data(),64))return false;
    ps2_stubs::zeroSifIopHeap(iop,128);
    if(!PS2IopTransport::readSoundMemory(rt.get(),0,result.data(),64)||result!=source)return false;
    if(call(0x80d0,0,1,iop+64,0,64)!=64)return false;
    if(!ps2_stubs::readSifIopHeap(iop+64,result.data(),64)||result!=source)return false;
    if(call(0x80d0,1,0,iop,0x1ffff0,64)!=UINT32_MAX || call(0x80f0,1,1,0,0,0)!=0)return false;
    if(call(0x80f0,0,1,0,0,0)!=1)return false; // Other core completion survives failure.
    if(call(0x80d0,1,0,0xffffffff,0,64)!=UINT32_MAX)return false;
    if(!PS2IopTransport::readSoundMemory(rt.get(),0,result.data(),64)||result!=source)return false;
    if(call(0x80d0,1,8,iop+64,0x20000,64)!=0)return false; // Programmed I/O returns zero.
    if(!PS2IopTransport::readSoundMemory(rt.get(),0x20000,result.data(),64)||result!=source)return false;
    if(call(0x8100,1,0,0,0,0)!=0x12f40)return false;
    ps2_stubs::sceSdRemoteInit(ram.data(),&ctx,rt.get());
    if(PS2IopTransport::readSoundMemory(rt.get(),0,result.data(),1))return false;
    if(!ps2_stubs::freeSifIopMemory(iop))return false;
    return true;
}
