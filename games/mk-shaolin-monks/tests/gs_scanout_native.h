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
