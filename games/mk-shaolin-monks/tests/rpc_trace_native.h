#include <filesystem>
#include <unordered_map>
#include <mutex>
#include "../../../ps2xRuntime/src/lib/Kernel/Syscalls/Helpers/State.h"
#include "../../../ps2xRuntime/src/lib/Kernel/Stubs/SIF.h"

static bool checkRpcAliasedPreview()
{
    auto rt=std::make_unique<PS2Runtime>();
    std::vector<uint8_t> ram(PS2_RAM_SIZE);
    R5900Context ctx{};
    ps2_stubs::resetSifState();
    SET_GPR_U32(&ctx,29,0x1e00000);
    ps2_syscalls::SifInitRpc(ram.data(),&ctx,rt.get());
    SET_GPR_U32(&ctx,4,0x10000);SET_GPR_U32(&ctx,5,0xf00d9876);SET_GPR_U32(&ctx,6,0);
    ps2_syscalls::SifBindRpc(ram.data(),&ctx,rt.get());
    for(bool alias:{false,true})
    {
        const uint32_t receive=alias?0x20000:0x30000;
        for(unsigned i=0;i<16;++i)ram[0x20000+i]=uint8_t(0x81+i);
        SET_GPR_U32(&ctx,4,0x10000);SET_GPR_U32(&ctx,5,0x42);SET_GPR_U32(&ctx,6,0);
        SET_GPR_U32(&ctx,7,0x20000);SET_GPR_U32(&ctx,8,16);
        SET_GPR_U32(&ctx,9,receive);SET_GPR_U32(&ctx,10,16);SET_GPR_U32(&ctx,11,0);
        ps2_syscalls::SifCallRpc(ram.data(),&ctx,rt.get());
        std::lock_guard lock(g_rpc_mutex);
        const auto& event=g_sif_rpc_debug_history[g_sif_rpc_debug_next_seq%kSifRpcDebugHistoryCount];
        if(event.sid!=0xf00d9876 || event.sendPreviewSize!=16 || event.recvPreviewSize!=16 ||
           !(event.flags&(alias?kSifRpcDebugFlagFallbackZero:kSifRpcDebugFlagFallbackCopy)))return false;
        for(unsigned i=0;i<16;++i)
            {
                const uint8_t expected=alias?0:uint8_t(0x81+i);
                if(event.sendPreview[i]!=uint8_t(0x81+i)||event.recvPreview[i]!=expected||ram[receive+i]!=expected)return false;
            }
    }
    const auto address=ps2_stubs::allocateSifIopMemory(128);
    if(!address || ps2_stubs::allocateSifIopMemory(0xffffffff)!=0)return false;
    std::array<uint8_t,128> payload{};payload.fill(0xa5);
    if(!ps2_stubs::writeSifIopHeap(address,payload.data(),payload.size()))return false;
    if(!ps2_stubs::freeSifIopMemory(address)||ps2_stubs::readSifIopHeap(address,payload.data(),1))return false;
    return true;
}
