#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>

// CRI's ring at codec+0x1198 (retail 0x44bd60/0x44c0d8): 2048-byte
// complete blocks plus a separate partial-block count. Feed owns no guest RAM.
template<class Feed>
uint32_t mkConsumeCriVideoBlocks(uint8_t* ram, uint32_t ramSize, uint32_t ring, Feed feed) {
    ring &= 0x1fffffffu;
    if(!ram || ramSize<0x48 || ring>ramSize-0x48)
        throw std::runtime_error("MKSM CRI ring outside EE RAM");
    auto read=[&](uint32_t offset) {uint32_t v;std::memcpy(&v,ram+ring+offset,4);return v;};
    auto write=[&](uint32_t offset,uint32_t v) {std::memcpy(ram+ring+offset,&v,4);};
    const uint32_t base=read(0)&0x1fffffffu, capacity=read(8);
    uint32_t head=read(12), count=read(16);
    if(!capacity || capacity>4096 || head>=capacity || count>capacity ||
       read(24)!=capacity*2048u || base>ramSize || capacity*2048u>ramSize-base)
        throw std::runtime_error("MKSM CRI video ring has invalid bounds");
    uint32_t consumed=0;
    while(count && feed(base+head*2048u,2048u)) {
        head=(head+1u)%capacity;
        --count;++consumed;
        write(12,head);write(16,count);
    }
    return consumed;
}
