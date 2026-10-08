#include "runtime/ps2_memory.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() try {
    PS2Memory memory;
    require(memory.initialize(), "memory initialization");
    auto* ram=memory.getRDRAM(); auto* spr=memory.getScratchpad();
    unsigned cases=0;
    for (auto alias : {0u,0x80000000u,0xa0000000u})
    for (bool receive : {true,false}) for (auto qwc : {0u,2u,0x14u,0x400u,0x801u}) {
        const uint32_t channel=receive?0x1000d000u:0x1000d400u, cause=receive?8u:9u;
        const uint32_t size=qwc*16, offset=0x80000, start=0x3ff0;
        std::fill(ram+offset-16,ram+offset+size+16,0xa5);
        for (uint32_t i=0;i<16384;++i) spr[i]=static_cast<uint8_t>(i*37+11);
        if (!receive) for(uint32_t i=0;i<size;++i) ram[offset+i]=static_cast<uint8_t>(i*19+(i>>14));
        std::vector<uint8_t> expectedSpr(spr,spr+16384), expectedRam(ram+offset,ram+offset+size);
        for(uint32_t i=0;i<size;++i) {
            if(receive) expectedRam[i]=spr[(start+i)&0x3fff];
            else expectedSpr[(start+i)&0x3fff]=ram[offset+i];
        }
        memory.writeIORegister(0x1000e000,0x21); // DMAE; fromSPR stall source.
        memory.writeIORegister(0x1000e010,0x3ff);
        memory.writeIORegister(channel+0x10,offset | alias);
        const uint32_t storedMadr=(offset|alias)&0x7fffffffu;
        require(memory.readIORegister(channel+0x10)==storedMadr,"SPR MADR bit 31 must read as zero");
        memory.writeIORegister(channel+0x20,qwc);
        memory.writeIORegister(channel+0x80,start);
        memory.writeIORegister(channel,receive?0x100:0x101);
        require(std::equal(expectedRam.begin(),expectedRam.end(),ram+offset),"RAM contents");
        require(std::equal(expectedSpr.begin(),expectedSpr.end(),spr),"SPR contents");
        require(ram[offset-1]==0xa5 && ram[offset+size]==0xa5,"RAM guards");
        require(memory.readIORegister(channel+0x10)==storedMadr+size,"MADR advancement");
        require(memory.readIORegister(channel+0x80)==((start+size)&0x3fff),"SADR wrap");
        require(memory.readIORegister(channel+0x20)==0 && !(memory.readIORegister(channel)&0x100),"completion registers");
        require((memory.readIORegister(0x1000e010)&(1u<<cause))!=0,"D_STAT completion");
        require(memory.consumeCompletedDmacCauses()==std::vector<uint32_t>{cause},"one completion event");
        if(receive) require(memory.readIORegister(0x1000e060)==storedMadr+size,"stall address");
        ++cases;
    }
    memory.writeIORegister(0x1000e000,0);
    memory.writeIORegister(0x1000d010,0x80000);
    memory.writeIORegister(0x1000d020,2);
    memory.writeIORegister(0x1000d000,0x100);
    require(memory.readIORegister(0x1000d020)==2 && memory.consumeCompletedDmacCauses().empty(),"disabled DMA does not complete");
    memory.writeIORegister(0x1000d000,0);
    memory.writeIORegister(0x1000e000,1);
    memory.writeIORegister(0x1000d010,PS2_RAM_SIZE-16);
    bool rejected=false;
    try { memory.writeIORegister(0x1000d000,0x100); } catch(const std::runtime_error&) { rejected=true; }
    require(rejected && memory.consumeCompletedDmacCauses().empty(),"invalid range must not complete");
    std::cout<<"PASS: "<<cases<<" SPR data/address cases; disabled DMA and invalid range checks\n";
}
 catch(const std::exception& error) {
    std::cerr<<"FAIL: "<<error.what()<<'\n';
    return 1;
}
