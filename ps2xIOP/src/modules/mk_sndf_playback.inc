// Private MkSndfControl implementation. Original functions: 268, 2278, 2508,
// 25bc, 26a4, 56f0, 5918, 1078, 1918; worker completion a478 -> 3644/1c18.
using Voices=std::array<SndfVoice,48>;
Voices m_voices{};
uint32_t m_pcmKey=0;
uint64_t m_submitted=0,m_completed=0;
unsigned m_outputMode=1;

void resetPlayback()
{
    if(m_pcmKey)m_host.pcmClose(m_pcmKey);
    m_pcmKey=0;m_submitted=m_completed=0;m_voices={};m_outputMode=1;
}

std::shared_ptr<const SndfSample> loadSample(unsigned bank,uint32_t start)
{
    const uint64_t begin=m_waveBases[bank],end=begin+m_banks[bank].waveSize;
    if(start<begin || uint64_t(start)+16>end || (start&15) || end>0x200000)return {};
    auto sample=std::make_shared<SndfSample>();
    sample->blocks.resize(size_t(end-start));
    if(!m_host.readSoundMemory(start,sample->blocks.data(),sample->blocks.size()))return {};
    for(size_t offset=0;offset+16<=sample->blocks.size();offset+=16){
        const auto* b=sample->blocks.data()+offset;
        if((b[0]>>4)>4)return {};
        if(b[1]&4)sample->loopBlock=offset;
        if(b[1]&1){sample->loop=(b[1]&2)!=0;sample->blocks.resize(offset+16);return sample;}
    }
    return {}; // Missing end/loop marker is not a synthetic completion.
}

bool prepareSound(SndfVoice& voice,uint32_t word,uint32_t stereo,unsigned requested)
{
    if(stereo || voice.prepared || voice.playing)return false;
    const unsigned first=requested==255?0:requested,last=requested==255?47:requested;
    if(first>=48)return false;
    for(unsigned slot=first;slot<=last;++slot){
        const auto& bank=m_banks[slot];if(!bank.loaded)continue;
        const auto& h=bank.header;const uint64_t table=read32(h.data()+0xb4);
        if(!table)continue;if(table+4>h.size())return false;
        for(unsigned entry=0;entry<=h[size_t(table)];++entry){
            const uint64_t p=table+4+entry*8;if(p+8>h.size())return false;
            const uint32_t group=read32(h.data()+size_t(p));if((group&65535)!=(word&65535))continue;
            const unsigned tone=(word>>16)&255;if(tone>=group>>16)return false;
            const uint64_t location=table+read32(h.data()+size_t(p)+4)+tone*4;
            if(location+4>h.size())return false;
            const uint32_t relative=read32(h.data()+size_t(location));
            if(!relative || ((relative&255)>=0xa9 && (relative&255)<=0xaf))return false;
            const uint64_t descriptor=table+relative;if(descriptor+28>h.size())return false;
            const auto* d=h.data()+size_t(descriptor);
            // Frontend descriptors have no modulation curve or noise sample.
            if(d[25]!=255 || read16(d+2)<256 || (d[8]&3)!=3)return false;
            if(bank.aliasSlot>=48 || !m_banks[bank.aliasSlot].loaded)return false;
            const auto& waveBank=m_banks[bank.aliasSlot];const auto& wh=waveBank.header;
            const uint64_t wave=uint64_t(read32(wh.data()+0xb8))+4+uint64_t(read16(d+2)-256)*16;
            if(wave+16>wh.size())return false;
            const uint64_t start=uint64_t(m_waveBases[bank.aliasSlot])+read32(wh.data()+size_t(wave))+16;
            if(start>UINT32_MAX)return false;
            auto sample=loadSample(bank.aliasSlot,uint32_t(start));if(!sample)return false;
            voice={};voice.prepared=true;voice.sample=std::move(sample);voice.bank=uint8_t(slot);
            voice.baseVolume=d[9];voice.cents=std::bit_cast<int16_t>(read16(d+10));
            voice.jitter=std::bit_cast<int16_t>(read16(d+12));if(voice.jitter<0)return false;
            voice.protectedVoice=(d[22]&128)!=0;
            voice.envelope.adsr1=read16(d+14);voice.envelope.adsr2=read16(d+16);
            voice.startAddress=uint32_t(start);return true;
        }
    }
    return false;
}

void stopVoices(Voices& voices,std::array<uint8_t,0x2b00>& status,int bank=-1)
{
    for(unsigned i=0;i<48;++i){auto& v=voices[i];
        if(!v.protectedVoice && (bank<0 || v.bank==unsigned(bank))){
            v.prepared=false;v.envelope.release();status[0x25cc+i*4]=status[0x25cd+i*4]=0;
        }
    }
}

// Original de2c -> 8340 retires the bank's channel-tone ownership. Sample
// storage remains shared with an already queued release tail until it drains.
int32_t unloadBank(unsigned slot)
{
    auto& bank=m_banks[slot];if(!bank.loaded)return -2;
    auto status=m_status;auto tones=m_tones;
    for(unsigned i=0;i<64;++i){auto* channel=status.data()+0x30c+slot*0x80+i*2;
        const unsigned index=read16(channel);if(index==65535)continue;
        if(index>=tones.size())return -8;
        auto& tone=tones[index];tone[0]=tone[1]=tone[2]=0;tone[15]=64;
        tone[16]=tone[17]=tone[18]=0;write32(tone.data()+20,0);write32(tone.data()+24,0);
        channel[0]=channel[1]=255;
    }
    auto voices=m_voices;stopVoices(voices,status,int(slot));
    if(m_statusAddress && !m_host.writeGuest(m_statusAddress,status.data(),status.size()))return -8;
    m_status=status;m_tones=tones;m_voices=std::move(voices);
    m_totalWaves-=bank.waveCount;bank={};return 0;
}

bool stageSound(std::span<const uint8_t> r,Voices& voices,std::array<uint8_t,0x2b00>& status,
                unsigned& mode,bool& starts)
{
    const unsigned family=read16(r.data());
    if(family==1){
        uint32_t command=read32(r.data()+12);
        if(command&128)command=(command<<24)|((command&0xff00)<<8)|((command>>8)&0xff00)|(command>>24);
        if(command==0xa0000100){
            // Sequenced/streamed SNDF voices are not admitted by this backend;
            // only direct voices can currently own playback resources.
            stopVoices(voices,status);return true;
        }
        if(command==0xa0000800)mode=1;else if(command==0xa0000900)mode=0;else return false;
        for(auto& v:voices)if(v.prepared)v.volume(status[0x1a+v.bank*16],mode);
        return true;
    }
    if(family!=3 || !m_initialized || !m_ready || !m_statusAddress)return false;
    const uint32_t op=read32(r.data()+4),index=read32(r.data()+8),a=read32(r.data()+12),b=read32(r.data()+16);
    if(op==0xf00 && ((a&0xffffff)==0x500a0 || (a&0xffffff)==0x1500a0)){
        stopVoices(voices,status,(a&0xffffff)==0x500a0?-1:int(a>>24));
        return true;
    }
    if(index>=48)return false;
    auto& v=voices[index];auto* s=status.data()+0x25cc+index*4;
    if(op==0xf00){
        if(!prepareSound(v,a,b,read16(r.data()+2)&255))return false;
        s[0]=1;s[2]=0;s[3]=127;return true;
    }
    if(op==0x1f00){v.prepared=false;v.envelope.release();s[0]=s[1]=0;return true;}
    if(op==0x1100){if(v.prepared && s[1]==1){v.envelope.release();s[1]=2;}s[2]=0;return true;}
    if(!v.prepared)return false;
    if(op==0x1200){
        if((a&65535)>180 || (b&65535)>255)return false;
        v.angle=mode==0?45:uint16_t(a);v.distance=uint16_t(b);
        v.volume(status[0x1a+v.bank*16],mode);s[2]=0;return true;
    }
    if(op==0x1d00){if((a&255)!=2)return false;v.mode=uint8_t(a);return true;}
    if(op==0x1000){
        if(s[1] || v.mode!=2)return false;
        int cents=v.cents;
        // sndfi f58 uses GetSystemTime's low word modulo twice the jitter.
        // The host monotonic clock supplies variation; its epoch is not PS2-identical.
        if(v.jitter){const auto ticks=std::chrono::steady_clock::now().time_since_epoch().count();
            cents+=int(uint32_t(ticks)%(unsigned(v.jitter)*2))-v.jitter;}
        v.pitch=SndfVoice::calculatePitch(cents,std::bit_cast<int32_t>(m_flags));
        v.volume(status[0x1a+v.bank*16],mode);
        if(!v.start())return false;
        s[1]=1;s[2]=0;starts=true;return true;
    }
    return false;
}

void pumpAudio()
{
    if(!m_pcmKey)return;
    const uint64_t consumed=std::min(m_submitted,m_host.pcmConsumed(m_pcmKey));
    for(unsigned i=0;i<48;++i){auto& v=m_voices[i];
        if(v.completionFrame && consumed>=v.completionFrame){
            v.completionFrame=0;v.prepared=false;v.sample.reset();
            m_status[0x25cc+i*4]=m_status[0x25cd+i*4]=0;++m_completed;
        }
    }
    while(m_submitted-consumed<1024){
        auto voices=m_voices;std::array<int16_t,256> pcm{};unsigned frames=0;
        const unsigned limit=unsigned(std::min<uint64_t>(128,1024-(m_submitted-consumed)));
        for(;frames<limit;++frames){
            int64_t left=0,right=0;bool active=false;
            for(auto& v:voices)if(v.playing){
                active=true;const int sample=v.render();
                left+=int64_t(sample)*v.gainLeft/16384;right+=int64_t(sample)*v.gainRight/16384;
                if(!v.playing)v.completionFrame=m_submitted+frames+1;
            }
            if(!active)break;
            pcm[frames*2]=int16_t(std::clamp<int64_t>(left,-32768,32767));
            pcm[frames*2+1]=int16_t(std::clamp<int64_t>(right,-32768,32767));
        }
        if(!frames || !m_host.pcmSubmit(m_pcmKey,std::span(pcm.data(),frames*2)))break;
        m_voices=std::move(voices);m_submitted+=frames;
    }
}
