#include "runtime/ee_scheduler.h"
#include "../runtime/output/sub_00414988_0x414988.cpp"
#include "../runtime/output/sub_004149F0_0x4149f0.cpp"
#include "../runtime/output/sub_004154F8_0x4154f8.cpp"
#include "../runtime/output/sub_00415580_0x415580.cpp"

// Execute the same retail chain as probe-cri-resume.py against real HLE syscalls.
static bool checkCriResume() {
    auto ownedRuntime=std::make_unique<PS2Runtime>();
    auto& runtime=*ownedRuntime;
    std::vector<uint8_t> ram(PS2_RAM_SIZE);
    R5900Context ctx{};
    SET_GPR_U32(&ctx,29,0x1e00000);
    auto& ee=runtime.eeScheduler();
    ee.reset(ram.data(),ctx);
    ee.bindMainContextForSyscall(ctx,ram.data());
    runtime.registerFunction(0x414988,sub_00414988_0x414988);
    runtime.registerFunction(0x4149f0,sub_004149F0_0x4149f0);
    runtime.registerFunction(0x414d08,[](uint8_t*,R5900Context* c,PS2Runtime*) {
        SET_GPR_U32(c,2,0);c->pc=getRegU32(c,31);
    });
    runtime.registerFunction(0x47fd60,[](uint8_t* r,R5900Context* c,PS2Runtime* rt) {
        ps2_syscalls::ReferThreadStatus(r,c,rt);c->pc=getRegU32(c,31);
    });
    runtime.registerFunction(0x47fdf0,[](uint8_t* r,R5900Context* c,PS2Runtime* rt) {
        ps2_syscalls::ResumeThread(r,c,rt);c->pc=getRegU32(c,31);
    });
    runtime.registerFunction(0x47fd90,[](uint8_t* r,R5900Context* c,PS2Runtime* rt) {
        ps2_syscalls::WakeupThread(r,c,rt);c->pc=getRegU32(c,31);
    });
    const int id=ee.createThread(EeThreadCreateParams{0,0x110000,0x24000,0x800,0,20,0});
    if(ee.startThread(id,0,ctx,false)!=0)return false;
    std::memcpy(ram.data()+0x5341cc,&id,4);
    for(bool asleep:{false,true}) {
        if(ee.suspendThread(id,false)!=0)return false;
        if(asleep) {
            // Seed the observed WAIT-SUSPEND state, with no ready-queue membership.
            ee.thread(id)->status=EeThreadStatus::WaitingSuspended;
            ee.thread(id)->wait.reason=EeWaitReason::Sleep;
        }
        ctx.pc=0x4154f8;SET_GPR_U32(&ctx,31,0x80000);
        sub_004154F8_0x4154f8(ram.data(),&ctx,&runtime);
        if(ctx.pc!=0x80000 || getRegU32(&ctx,29)!=0x1e00000 ||
           ee.thread(id)->status!=EeThreadStatus::Ready ||
           ee.thread(id)->suspendCount!=0 || ee.thread(id)->wait.reason!=EeWaitReason::None)
            return false;
    }
    // Original CRI main-release code clears its pending flag only on ID success.
    if(ee.suspendThread(id,false)!=0)return false;
    ee.thread(id)->status=EeThreadStatus::Waiting;
    ee.thread(id)->suspendCount=0;
    ee.thread(id)->wait.reason=EeWaitReason::Sleep;
    std::memcpy(ram.data()+0x5341c8,&id,4);
    uint32_t pending=1;std::memcpy(ram.data()+0x53417c,&pending,4);
    ctx.pc=0x415580;SET_GPR_U32(&ctx,31,0x80000);
    sub_00415580_0x415580(ram.data(),&ctx,&runtime);
    std::memcpy(&pending,ram.data()+0x53417c,4);
    if(pending!=0 || ee.thread(id)->status!=EeThreadStatus::Ready)return false;
    SET_GPR_S32(&ctx,4,id);
    ps2_syscalls::iWakeupThread(ram.data(),&ctx,&runtime);
    if(getRegU32(&ctx,2)!=static_cast<uint32_t>(id) || ee.thread(id)->wakeupCount!=1)return false;
    // Preserve signed errors for an invalid ID and a thread that is not suspended.
    for(int target:{0,id,999}) {
        const int expected=ee.resumeThread(target,false);
        if(expected>=0)return false;
        SET_GPR_S32(&ctx,4,target);
        ps2_syscalls::ResumeThread(ram.data(),&ctx,&runtime);
        auto* resultContext=&ctx;
        if(static_cast<int64_t>(GPR_U64(resultContext,2))!=expected)return false;
    }
    return true;
}
