#include "runtime/gs/gs_cpu_backend.h"
static bool checkGsInterlacedFrameRead() {
 std::vector<uint8_t> vram(4*1024*1024);
 auto gs=std::make_unique<GSCpuBackend>();gs->Initialize(vram.data(),uint32_t(vram.size()));
 // A row gradient and a different adjacent buffer expose an over-wide read.
 for(unsigned y=0;y<451;++y)for(unsigned x=0;x<64;++x)
   gs->WriteVram(GS_PSM_CT32,0,1,x,y,0x80000000u|y|((y>=227?0xe1u:0x32u)<<16));
 for(unsigned crt:{1u,2u})for(unsigned mode:{0u,2u,3u}) {
   GSPresentationRequest request{};request.pmode=crt;request.smode2=mode;
   const uint64_t frame=(1ull<<9)|(3ull<<43);
   const uint64_t display=(63ull<<32)|(447ull<<44);
   if(crt==1){request.dispfb1=frame;request.display1=display;}
   else {request.dispfb2=frame;request.display2=display;}
   auto result=gs->Present(request);
   if(result.width!=64 || result.height!=448)return false;
   for(unsigned y=0;y<448;++y) {
     const unsigned row=3+(mode==3?y/2:y);
     const auto* pixel=result.pixels.data()+y*640*4;
     if(pixel[0]!=uint8_t(row) || pixel[1]!=uint8_t(row>>8) || pixel[2]!=(row>=227?0xe1:0x32))return false;
   }
 }
 return true;
}

#include "runtime/gs/gs_field_weaver.h"
static bool checkGsFieldWeaving() {
 GSFieldWeaver weave;
 GSPresentationRequest request{};request.smode2=3;request.pmode=2;
 const auto field=[](unsigned red,unsigned height=8u) {
   PresentationFrame f;f.width=64;f.height=height;f.pixels.resize(640*512*4);
   for(unsigned y=0;y<height;++y)for(unsigned x=0;x<f.width;++x) {
     auto* p=f.pixels.data()+(y*640+x)*4;p[0]=red;p[1]=y/2;p[3]=255;
   }
   return f;
 };
 // Real content continues to change. Each arriving field updates only its
 // scanline parity, retaining the complementary field without a blur filter.
 auto first=field(10);weave.apply(first,request);
 request.vsyncTick=1;auto second=field(20);weave.apply(second,request);
 for(unsigned y=0;y<8;++y)if(second.pixels[y*640*4]!=(y%2?20:10))return false;
 request.vsyncTick=2;auto third=field(30);weave.apply(third,request);
 for(unsigned y=0;y<8;++y)if(third.pixels[y*640*4]!=(y%2?20:30))return false;
 // Progressive and full-height movie modes never inherit field history.
 for(unsigned mode:{0u,1u,2u}) {
   request.smode2=mode;auto progressive=field(40);auto expected=progressive.pixels;
   weave.apply(progressive,request);if(progressive.pixels!=expected)return false;
   request.smode2=3;auto fresh=field(50);expected=fresh.pixels;
   weave.apply(fresh,request);if(fresh.pixels!=expected)return false;
 }
 auto resized=field(60,10);auto expected=resized.pixels;
 weave.apply(resized,request);if(resized.pixels!=expected)return false;
 weave.reset();auto reset=field(70);expected=reset.pixels;
 weave.apply(reset,request);return reset.pixels==expected;
}

// The draw-environment pointer includes its GIF tag. Clear draws follow all
// eight state registers and must reach the GS through the normal DMA path.
static bool checkGsDrawEnvironmentClear() {
 auto runtime=std::make_unique<PS2Runtime>();auto& mem=runtime->memory();
 if(!mem.initialize() || !runtime->syncCoreSubsystems())return false;
 GSCpuBackend access;access.Initialize(mem.getGSVRAM(),PS2_GS_VRAM_SIZE);
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x){
  access.WriteVram(GS_PSM_CT32,0,1,x,y,0x80ffffff);
  access.WriteVram(GS_PSM_Z24,140*32,1,x,y,0xffffff);
 }
 const uint64_t packet[]={0x100000000000800eull,0xe,
  1ull<<16,0x4c,140ull|(1ull<<24),0x4e,0,0x18,
  (63ull<<16)|(31ull<<48),0x40,1,0x1a,1,0x46,0x50000,0x47,0,0x45,
  0x30000,0x47,6,0,0x3f80000080402010ull,1,0,5,
  (64ull<<4)|(32ull<<20),5,0x50000,0x47};
 for(uint32_t address:{0x90000u,0x20090000u,0x70000100u}) {
  if(address==0x70000100u){for(unsigned i=0;i<sizeof(packet)/8;++i)mem.write64(address+i*8,packet[i]);}
  else std::memcpy(mem.getRDRAM()+0x90000,packet,sizeof(packet));
  access.WriteVram(GS_PSM_CT32,0,1,12,10,0x80ffffff);
  access.WriteVram(GS_PSM_Z24,140*32,1,12,10,0xffffff);
  mem.writeIORegister(0x1000e000,1);
  R5900Context c{};SET_GPR_U32(&c,4,address);
  ps2_stubs::sceGsPutDrawEnv(mem.getRDRAM(),&c,runtime.get());mem.processPendingTransfers();runtime->gifArbiter().drain();
  if(getRegU32(&c,2)!=0 || access.ReadVram(GS_PSM_CT32,0,1,12,10)!=0x80402010u ||
     access.ReadVram(GS_PSM_Z24,140*32,1,12,10)!=0){std::cerr<<"draw-env address="<<std::hex<<address<<" color="<<access.ReadVram(GS_PSM_CT32,0,1,12,10)<<" depth="<<access.ReadVram(GS_PSM_Z24,140*32,1,12,10)<<std::dec<<"\n";return false;}
 }
 return true;
}
