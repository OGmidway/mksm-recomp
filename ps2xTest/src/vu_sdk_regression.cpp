#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "Kernel/Stubs/VU.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
#include "../../games/mk-shaolin-monks/tests/vu-sdk-cases.inc"
int main() {
    PS2Runtime runtime;std::vector<uint8_t> ram(PS2_RAM_SIZE);R5900Context ctx{};
    unsigned failed=0,total=0;
    auto check=[&](bool ok,const char* name){++total;if(!ok){++failed;std::cerr<<"FAIL "<<name<<" case "<<total<<'\n';}};
    for(const auto& item:normCases) for(bool alias:{false,true}) {
        const uint32_t source=0x1000,dest=alias?source:0x1100;
        std::memcpy(ram.data()+source,item.source,16);
        SET_GPR_U32(&ctx,4,dest);SET_GPR_U32(&ctx,5,source);
        ps2_stubs::sceVu0Normalize(ram.data(),&ctx,&runtime);
        float actual[4],expected[4];std::memcpy(actual,ram.data()+dest,16);std::memcpy(expected,item.expected,16);
        bool ok=true;for(unsigned c=0;c<4;++c)ok&=std::isfinite(actual[c])&&std::fabs(actual[c]-expected[c])<=1e-6f;
        check(ok,"retail XYZ normalization / W=0");
    }
    for(const auto& item:persCases) for(bool alias:{false,true}) {
        const uint32_t matrix=0x2000,source=0x2100,dest=alias?source:0x2200;
        std::memcpy(ram.data()+matrix,item.matrix,64);std::memcpy(ram.data()+source,item.source,16);
        SET_GPR_U32(&ctx,4,dest);SET_GPR_U32(&ctx,5,matrix);SET_GPR_U32(&ctx,6,source);SET_GPR_U32(&ctx,7,item.mode);
        ps2_stubs::sceVu0RotTransPers(ram.data(),&ctx,&runtime);
        check(std::memcmp(ram.data()+dest,item.expected,16)==0,"retail projection mode");
    }
    for(bool alias:{false,true}) {
        const uint32_t sources[3]={0x3000,0x3100,0x3200};const uint32_t dest=alias?sources[0]:0x3300;
        for(unsigned i=0;i<3;++i)std::memcpy(ram.data()+sources[i],normCases[i].source,16);
        SET_GPR_U32(&ctx,4,dest);for(unsigned i=0;i<3;++i)SET_GPR_U32(&ctx,5+i,sources[i]);
        ps2_stubs::sceVu0NormalLightMatrix(ram.data(),&ctx,&runtime);
        float actual[16],expected[16]{};std::memcpy(actual,ram.data()+dest,64);expected[15]=1;
        for(unsigned i=0;i<3;++i){float n[4];std::memcpy(n,normCases[i].expected,16);for(unsigned c=0;c<3;++c)expected[4*c+i]=-n[c];}
        bool ok=true;for(unsigned i=0;i<16;++i)ok&=std::isfinite(actual[i])&&std::fabs(actual[i]-expected[i])<=1e-6f;
        check(ok,"normal-light XYZ normalization");
    }
    for(const auto& item:dotCases) {
        std::memcpy(ram.data()+0x4000,item.left,16);std::memcpy(ram.data()+0x4100,item.right,16);
        SET_GPR_U32(&ctx,4,0x4000);SET_GPR_U32(&ctx,5,0x4100);
        ps2_stubs::sceVu0InnerProduct(ram.data(),&ctx,&runtime);
        uint32_t actual;std::memcpy(&actual,&ctx.f[0],4);
        check(actual==item.expected && getRegU32(&ctx,2)==item.expected,"retail XYZ inner product / both return registers");
    }
    std::cout<<(failed?"FAIL ":"PASS ")<<total<<" retail SDK scalar/alias cases; failures="<<failed<<'\n';return failed?1:0;
}
