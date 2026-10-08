#include <cmath>
#include <limits>
#include "../runtime/output/sub_00203830_0x203830.cpp"
#include "../runtime/output/sub_002036A8_0x2036a8.cpp"
#include "../runtime/output/sub_00202DB8_0x202db8.cpp"
#include "../runtime/output/sub_00202180_0x202180.cpp"
#include "../runtime/output/sub_002022B0_0x2022b0.cpp"
#include "../runtime/output/sub_002028F8_0x2028f8.cpp"
#include "../runtime/output/sub_00202650_0x202650.cpp"
#include "../runtime/output/sub_00205F20_0x205f20.cpp"
#include "../runtime/output/sub_00202C80_0x202c80.cpp"

static bool checkSndfPitchBase() {
 auto owned=std::make_unique<PS2Runtime>();auto& runtime=*owned;
 std::vector<uint8_t> ram(PS2_RAM_SIZE);
 runtime.registerFunction(0x2036a8,sub_002036A8_0x2036a8);
 runtime.registerFunction(0x202db8,sub_00202DB8_0x202db8);
 runtime.registerFunction(0x202180,sub_00202180_0x202180);
 runtime.registerFunction(0x2022b0,sub_002022B0_0x2022b0);
 runtime.registerFunction(0x205f20,sub_00205F20_0x205f20);
 auto bits=[](double value){uint64_t result;std::memcpy(&result,&value,8);return result;};
 for(float ratio:{0.5f,1.0f,2.0f,4.0f}) {
  R5900Context c{};SET_GPR_U32((&c),29,0x1e00000);
  auto call=[&](uint32_t pc,auto fn){c.pc=pc;SET_GPR_U32((&c),31,0x80000);fn(ram.data(),&c,&runtime);
   return c.pc==0x80000 && GPR_U32((&c),29)==0x1e00000;};
  c.f[12]=ratio;
  if(!call(0x203830,sub_00203830_0x203830))return false;
  SET_GPR_U64((&c),4,GPR_U64((&c),2));
  c.f[12]=123.0f;c.f[0]=-456.0f; // A float ABI replacement must fail this test.
  ps2_stubs::log(ram.data(),&c,&runtime);
  if(c.f[12]!=123.0f || c.f[0]!=-456.0f){std::cerr<<"log altered FPRs\n";return false;}
  SET_GPR_U64((&c),4,GPR_U64((&c),2));SET_GPR_U64((&c),5,0x3fe62e42fefa39efull);
  if(!call(0x2028f8,sub_002028F8_0x2028f8))return false;
  SET_GPR_U64((&c),4,GPR_U64((&c),2));SET_GPR_U64((&c),5,bits(1200.0));
  if(!call(0x202650,sub_00202650_0x202650))return false;
  SET_GPR_U64((&c),4,GPR_U64((&c),2));
  if(!call(0x202c80,sub_00202C80_0x202c80))return false;
  const int expected=ratio==0.5f?-1200:ratio==1.0f?0:ratio==2.0f?1200:2400;
  if(int32_t(GPR_U32((&c),2))!=expected){std::cerr<<"pitch ratio="<<ratio<<" actual="<<int32_t(GPR_U32((&c),2))<<" expected="<<expected<<"\n";return false;}
 }
 for(double value:{0.0,-1.0,std::numeric_limits<double>::infinity()}) {
  R5900Context c{};SET_GPR_U64((&c),4,bits(value));ps2_stubs::log(ram.data(),&c,&runtime);
  uint64_t raw=GPR_U64((&c),2);double result;std::memcpy(&result,&raw,8);
  if(value<0 ? !std::isnan(result) : !std::isinf(result) || std::signbit(result)!=(value==0))return false;
 }
 return true;
}
