#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace ps2x::iop
{
    // Raw SPU ADPCM blocks, also used by VAG containers. Predictor rounding and
    // saturation follow the SPU2 decoder reference (PCSX2, GPL-3.0+):
    // https://github.com/PCSX2/pcsx2/blob/master/pcsx2/SPU2/Mixer.cpp
    // No container, playback clock, envelope or interpolation is implied here.
    struct SpuAdpcmDecoder
    {
        int32_t previous = 0, older = 0;

        bool decode(std::span<const uint8_t> block, std::array<int16_t,28>& output)
        {
            if (block.size()!=16 || (block[0]>>4)>4) return false;
            constexpr int coefficients[5][2]={{0,0},{60,0},{115,-52},{98,-55},{122,-60}};
            const auto& c=coefficients[block[0]>>4];
            const unsigned shift=block[0]&15;
            for(unsigned i=0;i<28;++i)
            {
                const unsigned nibble=(block[2+i/2]>>(4*(i&1)))&15;
                const int32_t value=int32_t(nibble)-(nibble&8?16:0);
                // Multiplication avoids shifting negative values; division is
                // explicitly floored, including negative predictor feedback.
                const int32_t decoded=floorDivide(value*4096,1u<<shift)+
                    floorDivide(c[0]*previous+c[1]*older+32,64);
                older=previous;
                previous=std::clamp(decoded,-32768,32767);
                output[i]=static_cast<int16_t>(previous);
            }
            return true;
        }
    private:
        static int32_t floorDivide(int32_t value,uint32_t divisor)
        {
            return value>=0?value/int32_t(divisor):-int32_t((uint32_t(-value)+divisor-1)/divisor);
        }
    };
}
