#include "ps2_runtime.h"
#include "runtime/ps2_vu_clip.h"
#include "ps2_runtime_macros.h"
#include <cstring>
#include <iostream>
#include <vector>
void sub_0022C838_0x22c838(uint8_t*,R5900Context*,PS2Runtime*);
int main(){
 PS2Runtime runtime;std::vector<uint8_t> ram(PS2_RAM_SIZE);unsigned failures=0;
 struct Case{float x;uint32_t expected;};
 const Case cases[]={{0,1},{9.5f,2},{20,0},{-20,0}};
 for(auto item:cases){
  R5900Context c{};auto* ctx=&c;c.vu0_vf[0]=_mm_set_ps(1,0,0,0);c.f[12]=1;
  SET_GPR_U32(ctx,4,0x1000);SET_GPR_U32(ctx,29,0x3000);SET_GPR_U32(ctx,31,0x4000);
  float position[4]={item.x,0,0,1};std::memcpy(&c.r[5],position,16);
  const uint32_t enabled=1;std::memcpy(ram.data()+0x4f41b0,&enabled,4);
  const float planes[32]={1,-1,0,0, 0,0,1,0, 0,0,0,0, 10,10,10,0,
                         0,0,0,0, -1,0,0,0, 0,1,-1,0, 10,10,10,0};
  std::memcpy(ram.data()+0x1080,planes,sizeof(planes));
  sub_0022C838_0x22c838(ram.data(),&c,&runtime);
  if(GPR_U32(ctx,2)!=item.expected){++failures;std::cerr<<"FAIL retail sphere x="<<item.x<<" expected="<<item.expected<<" actual="<<GPR_U32(ctx,2)<<'\n';}
 }
 struct ClipCase {uint32_t xyz[4],w,history,expected;};
 const ClipCase clips[]={
#include "../../games/mk-shaolin-monks/tests/vu-clip-cases.inc"
 };
 unsigned clipCases=0;
 for(const auto& item:clips) for(bool alias:{false,true}) {
  R5900Context c{};const unsigned ft=alias?1:2;
  std::memcpy(&c.vu0_vf[1],item.xyz,16);
  uint32_t threshold[4]={0,0,0,item.w};
  if(alias)std::memcpy(reinterpret_cast<char*>(&c.vu0_vf[1])+12,&item.w,4);
  else std::memcpy(&c.vu0_vf[2],threshold,16);
  c.vu0_clip_flags=item.history;c.vu0_status=0xabc;c.vu0_mac_flags=0x1234;c.vu0_vpu_stat=0x100;
  const auto before=c;
  ps2VuClip(&c,0x4bc001ffu|(ft<<16)|(1u<<11));
  if(c.vu0_clip_flags!=item.expected||c.vu0_status!=before.vu0_status||c.vu0_mac_flags!=before.vu0_mac_flags||c.vu0_vpu_stat!=before.vu0_vpu_stat||std::memcmp(c.vu0_vf,before.vu0_vf,sizeof(c.vu0_vf)))++failures;
  ++clipCases;
 }
 std::cout<<"Retail sphere cases=4 clip scalar/alias cases="<<clipCases<<" failures="<<failures<<'\n';return failures?1:0;
}
