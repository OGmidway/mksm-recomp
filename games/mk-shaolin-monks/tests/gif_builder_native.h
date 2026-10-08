#pragma once
#include "../runtime/output/sub_003849A8_0x3849a8.cpp"
#include "../runtime/kernel-output/mk_recovered_00384d30_0x384d30.cpp"
#include "../runtime/kernel-output/mk_recovered_00384c60_0x384c60.cpp"
#include "../runtime/kernel-output/mk_recovered_00385360_0x385360.cpp"
#include "../runtime/kernel-output/mk_recovered_00385178_0x385178.cpp"
static bool checkGifBuilderFinalization() {
    auto runtime=std::make_unique<PS2Runtime>();std::vector<uint8_t> ram(PS2_RAM_SIZE);
    constexpr uint32_t state=0x20000,tag=0x21000;
    auto word=[&](uint32_t a,uint32_t v){std::memcpy(ram.data()+a,&v,4);};
    auto read=[&](uint32_t a){uint32_t v;std::memcpy(&v,ram.data()+a,4);return v;};
    for(uint32_t records:{1u,4u,16u}) {
        std::fill(ram.begin()+state,ram.begin()+tag+1024,0);
        word(state,tag+16);word(state+4,tag);word(state+8,tag);word(tag,0x70000000);
        for(uint32_t i=0;i<records;++i) {
            R5900Context c{};SET_GPR_U32(&c,4,state);SET_GPR_U32(&c,5,0x50+i);
            SET_GPR_U64(&c,6,0x1122334455660000ull+i);SET_GPR_U32(&c,31,0x80000);
            mk_recovered_00384d30_0x384d30(ram.data(),&c,runtime.get());
        }
        R5900Context c{};SET_GPR_U32(&c,4,state);SET_GPR_U32(&c,31,0x80000);
        sub_003849A8_0x3849a8(ram.data(),&c,runtime.get());
        if(read(tag)!=(0x70000000|records) || read(state+8)!=0 || read(state+4)!=tag)return false;
    }
    // Both GIF and VIF close their GIF tag without touching the open DMA count.
    for(bool vif:{false,true}) {
        std::fill(ram.begin()+state,ram.begin()+tag+1024,0);
        const uint32_t gif=tag+16;
        word(state,gif+16);word(state+4,tag);word(state+8,tag);
        word(state+(vif?20:12),gif);word(tag,0x70000000);
        const uint64_t gifTag=0x1000000000008000ull;std::memcpy(ram.data()+gif,&gifTag,8);word(gif+8,0xe);
        R5900Context c{};SET_GPR_U32(&c,4,state);SET_GPR_U32(&c,5,0x50);
        SET_GPR_U64(&c,6,0x1234567890abcdefull);SET_GPR_U32(&c,31,0x80000);
        if(vif)mk_recovered_00385360_0x385360(ram.data(),&c,runtime.get());
        else mk_recovered_00384d30_0x384d30(ram.data(),&c,runtime.get());
        SET_GPR_U32(&c,4,state);
        if(vif)mk_recovered_00385178_0x385178(ram.data(),&c,runtime.get());
        else mk_recovered_00384c60_0x384c60(ram.data(),&c,runtime.get());
        if(read(tag)!=0x70000000 || read(gif)!=0x8001 || read(state+(vif?20:12))!=0)return false;
        SET_GPR_U32(&c,4,state);sub_003849A8_0x3849a8(ram.data(),&c,runtime.get());
        if(read(tag)!=0x70000002)return false;
    }
    return true;
}
