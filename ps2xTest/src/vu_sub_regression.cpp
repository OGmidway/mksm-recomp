#include "ps2_runtime.h"
#include "runtime/ps2_vu_sub.h"
#include "ps2_runtime_macros.h"
#include <array>
#include <cstring>
#include <iostream>
struct Fixture { uint32_t op; std::array<uint32_t,4> left,right,result,flags; };
static const Fixture fixtures[]={
#include "../../games/mk-shaolin-monks/tests/vu-sub-cases.inc"
};
static void execute(R5900Context* ctx,uint32_t raw) {
ps2VuSub(ctx,raw);
}
int main() {
 unsigned cases=0;
 for(const auto& f:fixtures) for(unsigned mask=0;mask<16;++mask) for(unsigned dst:{0u,1u,2u,7u}) {
  R5900Context c{};
  c.vu0_vf[0]=_mm_set_ps(1,0,0,0);
  c.vu0_vf[7]=_mm_set_ps(-14,-13,-12,-11);
  std::memcpy(&c.vu0_vf[1],f.left.data(),16);std::memcpy(&c.vu0_vf[2],f.right.data(),16);
  c.vu0_q=c.vu0_i=3.0f;c.vu0_mac_flags=0xdead;c.vu0_status=0xa3f;
  std::array<uint32_t,4> expected;std::memcpy(expected.data(),&c.vu0_vf[dst],16);
  uint32_t mac=0,status=0;
  for(unsigned lane=0;lane<4;++lane) if(mask&(8u>>lane)) {
   if(dst)expected[lane]=f.result[lane];
   for(unsigned bit=0;bit<4;++bit)if(f.flags[lane]&(1u<<bit))mac|=(8u>>lane)<<(4*bit);
   status|=f.flags[lane];
  }
  execute(&c,0x4a000000u|(mask<<21)|(2u<<16)|(1u<<11)|(dst<<6)|f.op);
  std::array<uint32_t,4> actual;std::memcpy(actual.data(),&c.vu0_vf[dst],16);
  if(actual!=expected){std::cerr<<"FAIL VSUB destination op="<<f.op<<" mask="<<mask<<" dst="<<dst<<'\n';return 1;}
  const auto expectedMac=mac;
  const auto expectedStatus=(0xa3fu&0xff0u)|status|(status<<6);
  if(c.vu0_mac_flags!=expectedMac||c.vu0_status!=expectedStatus){std::cerr<<"FAIL VSUB flags op="<<f.op<<" mask="<<mask<<" dst="<<dst<<'\n';return 1;}
  if(c.vu0_vpu_stat!=0){std::cerr<<"FAIL changed VPU running status\n";return 1;}
  ++cases;
 }
 std::cout<<"PASS: "<<cases<<" VSUB mask/destination cases; VF0, aliasing, flags and edge values\n";
}
