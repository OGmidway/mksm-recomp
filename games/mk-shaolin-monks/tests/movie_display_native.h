#include "../runtime/kernel-output/mk_recovered_00383378_0x383378.cpp"
#include "../runtime/kernel-output/mk_recovered_00383560_0x383560.cpp"
#include "../runtime/kernel-output/mk_recovered_00383750_0x383750.cpp"
#include "../runtime/kernel-output/mk_recovered_003839e8_0x3839e8.cpp"
#include "../runtime/kernel-output/mk_recovered_00384090_0x384090.cpp"

static bool checkRetailMovieDisplay() {
 auto storage=std::make_unique<PS2Runtime>();auto& runtime=*storage;
 if(!runtime.memory().initialize())return false;
 auto& mem=runtime.memory();auto* ram=mem.getRDRAM();
 runtime.registerFunction(0x206258,[](uint8_t*,R5900Context* c,PS2Runtime*){SET_GPR_U32(c,2,0x4edc60);c->pc=getRegU32(c,31);});
 runtime.registerFunction(0x382f40,mk_recovered_00382f40_0x382f40);
 runtime.registerFunction(0x3832b8,mk_recovered_003832b8_0x3832b8);
 runtime.registerFunction(0x383378,mk_recovered_00383378_0x383378);
 runtime.registerFunction(0x383560,mk_recovered_00383560_0x383560);
 runtime.registerFunction(0x384090,mk_recovered_00384090_0x384090);
 // Native execution of the retail setup, using the live movie's field mode.
 const uint16_t params[]={1,2,0,3};std::memcpy(ram+0x4edc60,params,sizeof(params));
 std::memset(ram+0x90000,0,0x230);std::memset(ram+0x90230,0xcd,16);
 R5900Context c{};c.pc=0x383750;
 SET_GPR_U32(&c,4,0x90000);SET_GPR_U32(&c,5,0);SET_GPR_U32(&c,6,512);SET_GPR_U32(&c,7,448);
 SET_GPR_U32(&c,8,0);SET_GPR_U32(&c,9,0x30);SET_GPR_U32(&c,10,1);
 SET_GPR_U32(&c,29,0x1e00000);SET_GPR_U32(&c,31,0x80000);
 mk_recovered_00383750_0x383750(ram,&c,&runtime);
 // Independent Python execution of original 0x382f40 produced these values.
 const uint64_t expected[]={0x66,1,0x1000,0x1bf9ff0203227cull,0};
 if(c.pc!=0x80000 || getRegU32(&c,29)!=0x1e00000)return false;
 if(std::memcmp(ram+0x90000,expected,40)||std::memcmp(ram+0x90028,expected,40))return false;
 auto q=[&](unsigned a){uint64_t v;std::memcpy(&v,ram+0x90000+a,8);return v;};
 if(q(0x80)!=(uint64_t(1792*16)|(uint64_t(1824*16)<<32)))return false;
 if(q(0x90)!=((511ull<<16)|(447ull<<48)))return false;
 if((q(0x70)&0x1ff)!=112)return false; // Z buffer follows one 512x448 field buffer.
 if(q(0x50)!=0x100000000000800eull || q(0x140)!=q(0x50))return false;
 for(unsigned i=0;i<16;++i)if(ram[0x90230+i]!=0xcd)return false;
 return true;
}
