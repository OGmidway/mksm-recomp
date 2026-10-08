// Execute one captured VU1 call without booting the game. Exploratory only:
// captures omit pipeline queues and GS state, so resumed/budget-sliced calls
// are not guaranteed to reproduce the live machine.
#include "runtime/ps2_vu1.h"
#include "runtime/ps2_memory.h"
#include "runtime/gs/gs_frontend.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

static uint32_t word(std::istream& in) {
    unsigned char b[4];
    if (!in.read(reinterpret_cast<char*>(b), 4)) throw std::runtime_error("truncated capture");
    return uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
}
static float scalar(uint32_t bits) { float value; std::memcpy(&value,&bits,4); return value; }
int main(int argc,char** argv) try {
    if(argc!=4 && argc!=5)throw std::runtime_error("usage: ps2_vu_replay capture.bin record_index output_data.bin [cycle_budget]");
    const unsigned selected=static_cast<unsigned>(std::stoul(argv[2]));
    if(selected>=8)throw std::runtime_error("record index must be 0..7");
    const uint32_t budget=argc==5?static_cast<uint32_t>(std::stoul(argv[4])):1000000u;
    if(budget==0 || budget>1000000u)throw std::runtime_error("cycle budget must be 1..1000000");
    const auto input=std::filesystem::u8path(argv[1]);
    if(std::filesystem::file_size(input)>267520)throw std::runtime_error("capture exceeds bound");
    std::ifstream in(input,std::ios::binary);
    for(unsigned index=0;index<=selected;++index) {
        std::array<uint32_t,12> h{};for(auto& x:h)x=word(in);
        if(h[0]!=0x31555650u || (h[1]!=1&&h[1]!=2) || h[8]!=155 || h[5]!=16384 || h[6]!=16384 || h[2]>=16384 || h[2]%8)
            throw std::runtime_error("unsupported VU record");
        const uint32_t resumed=h[1]==2?word(in):0;
        if(resumed>1)throw std::runtime_error("invalid resume flag");
        std::array<uint32_t,155> values{};for(auto& x:values)x=word(in);
        std::vector<uint8_t> code(h[5]),data(h[6]);
        if(!in.read(reinterpret_cast<char*>(code.data()),code.size()) || !in.read(reinterpret_cast<char*>(data.data()),data.size()))
            throw std::runtime_error("truncated code/data");
        if(index!=selected)continue;
        auto memory=std::make_unique<PS2Memory>();
        if(!memory->initialize())throw std::runtime_error("memory initialization failed");
        GS gs;gs.init(memory->getGSVRAM(),static_cast<uint32_t>(PS2_GS_VRAM_SIZE),&memory->gs());
        auto vu=std::make_unique<VU1Interpreter>();auto& state=vu->state();
        unsigned at=0;
        for(auto& reg:state.vf)for(auto& lane:reg)lane=scalar(values[at++]);
        for(auto& reg:state.vi){const auto bits=values[at++];std::memcpy(&reg,&bits,4);}
        for(auto& lane:state.acc)lane=scalar(values[at++]);
        state.q=scalar(values[at++]);state.p=scalar(values[at++]);state.i=scalar(values[at++]);
        state.r=values[at++];state.mac=values[at++];state.clip=values[at++];state.status=values[at++];
        state.dBitEnabled=h[9]!=0;state.tBitEnabled=h[10]!=0;
        // execute initializes empty scheduling queues, including the captured clip history.
        vu->execute(code.data(),h[5],data.data(),h[6],gs,nullptr,h[2],h[3],h[4],budget);
        std::ofstream out(std::filesystem::u8path(argv[3]),std::ios::binary|std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()),data.size());
        if(!out)throw std::runtime_error("failed to write result data");
        std::cout<<"record="<<selected<<" originally_resumed="<<resumed<<" entry_pc=0x"<<std::hex<<h[2]
                 <<" final_pc=0x"<<state.pc<<std::dec<<" cycles="<<state.cycles<<" data_bytes="<<data.size()<<'\n';
    }
    return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
