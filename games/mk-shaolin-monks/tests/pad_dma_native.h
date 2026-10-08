#include "../runtime/output/sub_00212590_0x212590.cpp"
#include "../../../ps2xRuntime/src/lib/Kernel/Stubs/Pad.h"
static bool checkPadDmaStatus() {
 auto runtime=std::make_unique<PS2Runtime>();auto& mem=runtime->memory();
 if(!mem.initialize())return false;
 auto* ram=mem.getRDRAM();std::memset(ram+0x651f00,0xcd,0x110);
 R5900Context ctx{};SET_GPR_U32(&ctx,4,0);SET_GPR_U32(&ctx,5,0);SET_GPR_U32(&ctx,6,0x651f00);
 ps2_stubs::scePadPortOpen(ram,&ctx,runtime.get());
 if(getRegU32(&ctx,2)!=1)return false;
 for(uint16_t buttons:{uint16_t(65535),uint16_t(49151),uint16_t(65535)}) {
   ps2_stubs::setPadOverrideState(buttons,128,128,128,128);
   SET_GPR_U32(&ctx,6,0x90000);ps2_stubs::scePadRead(ram,&ctx,runtime.get());
   if(std::memcmp(ram+0x651f00,ram+0x90000,32) || std::memcmp(ram+0x651f80,ram+0x90000,32))return false;
   if(ram[0x652000]!=0xcd || ram[0x651f20]!=0xcd || ram[0x651fa0]!=0xcd)return false;
   // Run the actual retail wrapper after its low-level shell poll, with state
   // fields set as the successful live initialization reports them.
   runtime->registerFunction(0x272f48,[](uint8_t*,R5900Context* c,PS2Runtime*){c->pc=getRegU32(c,31);});
   mem.write32(0x65202c,3);mem.write16(0x91000,65535);
   ctx={};ctx.pc=0x212590;SET_GPR_U32(&ctx,4,0);SET_GPR_U32(&ctx,5,0x91000);SET_GPR_U32(&ctx,6,0);
   SET_GPR_U32(&ctx,29,0x1f00000);SET_GPR_U32(&ctx,31,0x80000);
   sub_00212590_0x212590(ram,&ctx,runtime.get());
   if(mem.read16(0x91004)!=(buttons==49151?64:0))return false;
   SET_GPR_U32(&ctx,4,0);SET_GPR_U32(&ctx,5,0);
 }
 ps2_stubs::clearPadOverrideState();return true;
}
