#include "runtime/ps2_memory.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/gs/ps2_gs_psmct32.h"
#include <array>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>
static void word(std::vector<uint8_t>& p,uint32_t v){for(int n=0;n<4;++n)p.push_back(uint8_t(v>>(n*8)));}
static void tag(std::vector<uint8_t>& p){word(p,0x8002);word(p,0x08000000);word(p,0);word(p,0);}
int main(){int failures=0;
for(int mode=0;mode<9;++mode){
 auto mem=std::make_unique<PS2Memory>();if(!mem->initialize())return 2;
 GS gs;gs.init(mem->getGSVRAM(),uint32_t(PS2_GS_VRAM_SIZE),&mem->gs());
 GifArbiter arbiter([&](const uint8_t* p,uint32_t n){gs.processGIFPacket(p,n);});mem->setGifArbiter(&arbiter);
 gs.writeRegister(GS_REG_BITBLTBUF,(1ull<<16)|(1ull<<48));gs.writeRegister(GS_REG_TRXPOS,0);gs.writeRegister(GS_REG_TRXREG,8ull|(1ull<<32));gs.writeRegister(GS_REG_TRXDIR,0);
 int calls=0;uint32_t pc=0;mem->setVu1MscalCallback([&](uint32_t v,uint32_t,uint32_t){++calls;pc=v;});
 std::vector<uint8_t> pixels;for(int i=0;i<8;++i)word(pixels,0x142106bdu+uint32_t(i));
 std::vector<uint8_t> p;word(p,0x50000000u+(mode==0||mode==4||mode>=5?3:mode==3?2:1));
 if(mode==4){word(p,1);word(p,0x10000000);word(p,0xe);word(p,0);word(p,1);word(p,0);word(p,0x46);word(p,0);}
 tag(p);
 auto emit=[&](){mem->processVIF1Data(p.data(),uint32_t(p.size()));p.clear();};
 if(mode==0||mode>=5)p.insert(p.end(),pixels.begin(),pixels.end());
 else {
   if(mode==3)p.insert(p.end(),pixels.begin(),pixels.begin()+16);
   if(mode==2)emit();
   word(p,0);word(p,0x07001234);word(p,0x04000123);
   if(mode==1||mode==4){word(p,0x50000002);p.insert(p.end(),pixels.begin(),pixels.end());}
   else if(mode==2){word(p,0x50000001);p.insert(p.end(),pixels.begin(),pixels.begin()+16);emit();word(p,0x51000001);p.insert(p.end(),pixels.begin()+16,pixels.end());}
   else {word(p,0x51000001);p.insert(p.end(),pixels.begin()+16,pixels.end());}
 }
 if(mode>=5)word(p,0x04000123);
 word(p,0x07005678);word(p,0x14000100);
 if(mode>=5){const size_t splits[]={28,12,4,5};const size_t split=splits[mode-5];mem->processVIF1Data(p.data(),uint32_t(split));mem->processVIF1Data(p.data()+split,uint32_t(p.size()-split));p.clear();}else emit();
 arbiter.drain();
 bool ok=calls==1&&pc==0x800&&mem->vif1_regs.mark==0x5678;
 if(mode)ok=ok&&mem->vif1_regs.itops==0x123;
 for(unsigned x=0;x<8;++x){auto a=GSPSMCT32::addrPSMCT32(0,1,x,0);ok=ok&&std::memcmp(mem->getGSVRAM()+a,pixels.data()+x*4,4)==0;}
 if(!ok)++failures;std::cout<<"mode="<<mode<<" calls="<<calls<<" mark="<<std::hex<<mem->vif1_regs.mark<<std::dec<<" pass="<<ok<<'\n';
}
std::cout<<"VIF image boundary cases=9 failures="<<failures<<'\n';return failures?1:0;}
