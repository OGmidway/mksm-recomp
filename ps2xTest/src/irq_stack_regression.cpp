#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include <cstring>
#include <iostream>
#include <vector>
namespace {
constexpr uint32_t entry=0x110000, resume=0x110010, handler=0x110020;
constexpr uint32_t ownerSp=0x1fffeb0, registeredSp=0x1ffff90, sentinel=0x179bfb8;
uint32_t observed=0, callbackSp=0;
bool threadVf0Valid=true, invocationVf0Valid=false;
void begin(uint8_t* ram,R5900Context* ctx,PS2Runtime* rt) {
    std::memcpy(ram+ownerSp+16,&sentinel,4);
    auto& scheduler=rt->eeScheduler();
    EeThreadCreateParams params{};params.entry=handler;params.stack=0x1800000;params.stackSize=0x1000;params.priority=127;
    const int child=scheduler.createThread(params);
    const uint32_t vf0Expected[4]={0,0,0,0x3f800000};
    for(unsigned attempt=0;attempt<2;++attempt) {
        threadVf0Valid &= scheduler.startThread(child,0,*ctx,true)==0;
        auto* thread=scheduler.thread(child);
        threadVf0Valid &= thread && std::memcmp(&thread->context.vu0_vf[0],vf0Expected,16)==0;
        uint32_t ownedStack=0;scheduler.terminateThread(child,ownedStack,true);
        if(thread)thread->context.vu0_vf[0]=_mm_set1_ps(-42.0f);
    }

    const int id=scheduler.addIrqHandler(false,2,handler,true,0,0,registeredSp);
    scheduler.setIrqHandlerEnabled(false,id,true);
    scheduler.setIrqCauseEnabled(false,2,true);
    ctx->pc=resume;
    scheduler.dispatchIrq(false,2);
}
void interrupt(uint8_t* ram,R5900Context* ctx,PS2Runtime*) {
    const uint32_t vf0Expected[4]={0,0,0,0x3f800000};
    invocationVf0Valid=std::memcmp(&ctx->vu0_vf[0],vf0Expected,16)==0;
    callbackSp=getRegU32(ctx,29);
    const uint32_t overwritten=0xf82;
    // Model an interrupt's downward-growing frame, as observed in MKSM.
    if(callbackSp>=0xd0 && callbackSp<PS2_RAM_SIZE)
        std::memcpy(ram+callbackSp-0xd0,&overwritten,4);
    ctx->pc=0;
}
void finish(uint8_t* ram,R5900Context* ctx,PS2Runtime* rt) {
    std::memcpy(&observed,ram+ownerSp+16,4);
    ctx->pc=0;rt->eeScheduler().requestStop();
}
}
int main() {
    PS2Runtime runtime;
    std::vector<uint8_t> ram(PS2_RAM_SIZE);
    runtime.registerFunction(entry,begin);runtime.registerFunction(resume,finish);
    runtime.registerFunction(handler,interrupt);
    R5900Context context{};context.pc=entry;
    context.r[29]=_mm_set_epi64x(0,ownerSp);
    runtime.eeScheduler().reset(ram.data(),context);
    runtime.eeScheduler().run();
    const bool irqOk=observed==sentinel && callbackSp!=registeredSp && callbackSp!=0;
    const bool ok=threadVf0Valid && invocationVf0Valid && irqOk;
    std::cout << (irqOk?"PASS":"FAIL") << " IRQ preserves interrupted stack: saved=" << std::hex
              << observed << " callback-sp=" << callbackSp << std::endl;
    std::cout<<(threadVf0Valid?"PASS":"FAIL")<<" started/restarted thread VF0 constant\n";
    std::cout<<(invocationVf0Valid?"PASS":"FAIL")<<" interrupt invocation VF0 constant\n";
    return ok?0:1;
}
