#pragma once
#include "ps2x/iop/spu_adpcm.h"
#include <algorithm>
#include <array>
#include <span>
#include <cstdint>
#include <memory>
#include <vector>

namespace ps2x::iop::detail
{
    #include "mk_sndf_tables.inc"

    // SPU envelope arithmetic reference: PCSX2 SPU2/ADSR.cpp (GPL-3.0+).
    // Output is advanced in source-device frames, never by an RPC-call timer.
    struct SndfEnvelope
    {
        uint16_t adsr1=0,adsr2=0;
        int phase=0,level=0;
        unsigned counter=0;
        void start(){phase=1;level=0;counter=0;}
        void release(){if(phase){phase=4;counter=0;}}
        int advance()
        {
            if(!phase)return 0;
            int shift,step,target;bool exponential,decrease;
            if(phase==1){shift=(adsr1>>10)&31;step=7-((adsr1>>8)&3);target=32767;exponential=adsr1&0x8000;decrease=false;}
            else if(phase==2){shift=(adsr1>>4)&15;step=-8;target=((adsr1&15)+1)*2048;exponential=true;decrease=true;}
            else if(phase==3){shift=(adsr2>>8)&31;step=7-((adsr2>>6)&3);target=0;exponential=adsr2&0x8000;decrease=adsr2&0x4000;if(decrease)step=~step;}
            else{shift=adsr2&31;step=-8;target=0;exponential=adsr2&32;decrease=true;}
            unsigned increment=0x8000u>>std::max(0,shift-11);
            int delta=step*(1<<std::max(0,11-shift));
            if(exponential){if(!decrease && level>0x6000)increment>>=2;if(decrease)delta=(delta*level)>>15;}
            counter+=std::max(1u,increment);
            if(counter>=0x8000){counter=0;level=std::clamp(level+delta,0,32767);}
            if(phase==3){if(!level)phase=0;}
            else if(decrease?level<=target:level>=target){if(++phase>4){phase=0;level=0;}}
            return level;
        }
    };

    struct SndfSample
    {
        std::vector<uint8_t> blocks;
        size_t loopBlock=0;
        bool loop=false;
    };

    struct SndfVoice
    {
        bool prepared=false,playing=false,hasNext=false,sourceEnded=false;
        uint8_t bank=0,mode=2,baseVolume=0,protectedVoice=0;
        uint16_t angle=45,distance=64,pitch=4096;
        int16_t cents=0,jitter=0;
        int gainLeft=0,gainRight=0;
        uint32_t startAddress=0;
        uint64_t completionFrame=0;
        std::shared_ptr<const SndfSample> sample;
        SpuAdpcmDecoder decoder;
        SndfEnvelope envelope;
        std::array<int16_t,28> blockPcm{};
        size_t blockOffset=0;
        unsigned blockPosition=28,fraction=0;
        int current=0,next=0;

        static uint16_t calculatePitch(int cents,int base)
        {
            const int64_t relative=int64_t(cents)-base;
            if(relative>=2400)return 0x3fff;
            uint32_t factor=384,index=0;
            if(relative>=0){factor<<=unsigned(relative/1200);index=unsigned(relative%1200);}
            else{
                const uint64_t value=uint64_t(-relative),octaves=value/1200+1;
                factor=octaves>=32?0:factor>>unsigned(octaves);
                if(value%1200)index=1200-unsigned(value%1200);else factor<<=1;
            }
            return uint16_t(std::clamp(factor*SndfPitchTable[index]/3072,1u,0x3fffu));
        }
        void volume(uint8_t bankVolume,unsigned outputMode)
        {
            unsigned value=unsigned(baseVolume)*255>>8;if(value)++value;
            value=value*bankVolume>>7;if(value)++value;
            value=value*127>>7;if(value)++value; // logical voice volume
            value=value*127>>7;if(value)++value; // driver master volume
            const unsigned pan=angle>90?180-angle:angle;
            const unsigned attenuation=distance<64?191:255-distance;
            gainLeft=int(((value*64*SndfPanTable[90-pan])/100)*attenuation/191)&0x3fff;
            gainRight=int(((value*64*SndfPanTable[pan])/100)*attenuation/191)&0x3fff;
            if(outputMode==0)gainLeft=gainRight=(gainLeft+gainRight)/2;
        }
        bool readSample(int& value)
        {
            if(blockPosition==28){
                if(sourceEnded || !sample || blockOffset+16>sample->blocks.size())return false;
                const auto* b=sample->blocks.data()+blockOffset;
                if(!decoder.decode(std::span(b,16),blockPcm))return false;
                blockOffset+=16;blockPosition=0;
                if(b[1]&1){if(sample->loop)blockOffset=sample->loopBlock;else sourceEnded=true;}
            }
            value=blockPcm[blockPosition++];return true;
        }
        bool start()
        {
            decoder={};blockOffset=0;blockPosition=28;fraction=0;sourceEnded=false;completionFrame=0;
            if(!readSample(current))return false;
            hasNext=readSample(next);if(!hasNext)next=current;
            envelope.start();playing=true;return true;
        }
        int render()
        {
            if(!playing)return 0;
            const int level=envelope.advance();
            if(!envelope.phase){playing=false;return 0;}
            // Linear interpolation is the initial native mixer; hardware Gaussian
            // interpolation/reverb are not implemented by this frontend path.
            const int value=current+(next-current)*int(fraction)/4096;
            fraction+=pitch;
            while(fraction>=4096 && playing){
                fraction-=4096;
                if(!hasNext){playing=false;envelope.phase=0;envelope.level=0;break;}
                current=next;hasNext=readSample(next);if(!hasNext)next=current;
            }
            return value*level/32768;
        }
    };
}
