#include "runtime/ps2_memory.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include "ps2x/iop/spu_adpcm.h"

namespace ps2_vag
{
    bool decode(const uint8_t *data, uint32_t sizeBytes,
                std::vector<int16_t> &outPcm, uint32_t &outSampleRate)
    {
        if (!data || sizeBytes < 48)
            return false;

        const uint32_t magic = (static_cast<uint32_t>(data[0]) << 24) |
                               (static_cast<uint32_t>(data[1]) << 16) |
                               (static_cast<uint32_t>(data[2]) << 8) |
                               static_cast<uint32_t>(data[3]);
        if (magic != 0x56414770u)
        {
            const uint32_t magicLE = (static_cast<uint32_t>(data[3]) << 24) |
                                     (static_cast<uint32_t>(data[2]) << 16) |
                                     (static_cast<uint32_t>(data[1]) << 8) |
                                     static_cast<uint32_t>(data[0]);
            if (magicLE != 0x56414770u)
                return false;
        }

        uint32_t dataSize = (static_cast<uint32_t>(data[0x0c]) << 24) |
                            (static_cast<uint32_t>(data[0x0d]) << 16) |
                            (static_cast<uint32_t>(data[0x0e]) << 8) |
                            static_cast<uint32_t>(data[0x0f]);
        outSampleRate = (static_cast<uint32_t>(data[0x10]) << 24) |
                        (static_cast<uint32_t>(data[0x11]) << 16) |
                        (static_cast<uint32_t>(data[0x12]) << 8) |
                        static_cast<uint32_t>(data[0x13]);
        if (outSampleRate == 0)
            outSampleRate = 44100;

        // A declared size is not permission to read or allocate beyond the
        // supplied container. The payload consists of complete 16-byte blocks.
        if (dataSize > sizeBytes-48 || (dataSize&15)) return false;
        const uint32_t numBlocks=dataSize/16;
        std::vector<int16_t> decoded;
        decoded.reserve(size_t(numBlocks)*28);
        ps2x::iop::SpuAdpcmDecoder decoder;
        std::array<int16_t,28> blockPcm{};
        for(uint32_t b=0;b<numBlocks;++b)
        {
            const auto* block=data+48+b*16;
            if(!decoder.decode(std::span(block,16),blockPcm))return false;
            decoded.insert(decoded.end(),blockPcm.begin(),blockPcm.end());
        }
        outPcm=std::move(decoded);

        return true;
    }
}
