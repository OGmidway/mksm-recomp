#pragma once

#include "iop_service.h"
#include "mk_sndf_voice.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <sstream>
#include <bit>
#include <chrono>

namespace ps2x::iop::detail
{
    // Retail sndfi.irx: b678 configuration, b7b8 initialization, c3d0 reset,
    // e400 partitioning, bae0 RPC dispatch. No synthesized playback completion.
    class MkSndfControl
    {
    public:
        explicit MkSndfControl(IopHost& host) : m_host(host) { resetStatus(); }
        ~MkSndfControl() { resetPlayback(); release(); }
        void reset()
        {
            resetPlayback();
            release();
            m_size = m_external = m_statusAddress = m_rate = m_flags = 0;
            m_spuSize = 0x1c7fc0;
            m_ready = m_initialized = false;
            m_headerCaps.fill(0); m_waveCaps.fill(0);
            m_headerBases.fill(0); m_waveBases.fill(0);
            resetStatus();
        }
        RpcResult handle(const RpcRequest& r)
        {
            const uint32_t op = r.function & 0xff00u;
            const bool registerRequest=op==0xa00 && r.function<0xa30;
            const bool unloadRequest=op==0x700 && r.function<0x730;
            uint32_t inputBytes = 0, outputBytes = 4;
            switch (r.function)
            {
                case 0x1300: inputBytes = 24; break;
                case 0x100: inputBytes = 8; outputBytes = 8; break;
                case 0x1600: inputBytes = 4; break;
                case 0x300: inputBytes = 384; break;
                case 0xfe00: outputBytes = 384; break;
                default: if (!registerRequest && !unloadRequest) return {}; break;
            }
            if (r.send.size != inputBytes || r.receive.size != outputBytes ||
                !r.receive.address || r.receive.address > UINT32_MAX - outputBytes ||
                (inputBytes && (!r.send.address || r.send.address > UINT32_MAX - inputBytes)))
                return {};
            std::array<uint8_t, 384> input{}, reply{};
            // Validate both buffers and preserve aliased inputs before writing a reply.
            if ((inputBytes && !m_host.readGuest(r.send.address, input.data(), inputBytes)) ||
                !m_host.readGuest(r.receive.address, reply.data(), outputBytes)) return {};
            reply.fill(0);
            int32_t result = 0;
            if (unloadRequest) result=unloadBank(r.function & 63);
            else if (registerRequest)
            {
                result=registerBank(r.function & 63);
            }
            else if (r.function == 0x1300)
            {
                if (!read32(input.data())) result = -1;
                else
                {
                    resetPlayback();
                    m_size = read32(input.data());
                    m_external = read32(input.data()+4);
                    m_spuSize = read32(input.data()+20);
                    if (!m_spuSize) m_spuSize = 0x1c7fc0;
                }
            }
            else if (r.function == 0x100)
            {
                // The backing store is IOP memory, never the game's EE heap.
                uint32_t base = 0, queue = 0;
                if (m_size && m_size <= 0x200000)
                {
                    if (m_external)
                    {
                        std::vector<uint8_t> check(m_size);
                        if (m_external <= UINT32_MAX-m_size &&
                            m_host.readGuest(m_external, check.data(), check.size())) base=m_external;
                    }
                    else base=m_host.allocateIopMemory(m_size);
                    if (base) queue=m_host.allocateIopMemory(0xa40);
                }
                if (!base || !queue)
                {
                    if (base && !m_external) m_host.freeIopMemory(base);
                    result=-2;
                }
                else
                {
                    resetPlayback();
                    release();
                    m_base=base; m_ownsBase=!m_external; m_queue=queue;
                    m_rate=read32(input.data()); m_flags=read32(input.data()+4);
                    m_headerCaps.fill(0); m_waveCaps.fill(0);
                    m_headerBases.fill(0); m_waveBases.fill(0);
                    m_headerBases[0]=base; m_waveBases[0]=0x20000;
                    m_statusAddress=0; m_ready=false; m_initialized=true;
                    resetStatus();
                    write32(reply.data()+4,base);
                    std::ostringstream text;
                    text << "[MK SNDF] initialized rate=" << m_rate << " work=0x" << std::hex
                         << base << "/" << m_size << " queue=0x" << queue;
                    m_host.log(LogLevel::Info,text.str());
                }
            }
            else if (r.function == 0x1600)
            {
                const uint32_t destination=read32(input.data());
                if (!m_initialized || !destination || destination > UINT32_MAX-m_status.size() ||
                    !m_host.writeGuest(destination,m_status.data(),m_status.size())) return {};
                m_statusAddress=destination;
                result=static_cast<int32_t>(m_queue);
            }
            else if (r.function == 0x300)
            {
                if (!m_initialized) return {};
                uint64_t headerTotal=0, waveTotal=0;
                for (unsigned i=0;i<48;++i)
                {
                    headerTotal+=read32(input.data()+i*4);
                    waveTotal+=read32(input.data()+192+i*4);
                    if (headerTotal>m_size) { result=-1; break; }
                    if (waveTotal>m_spuSize || waveTotal>0x1e0000) { result=-5; break; }
                }
                if (!result)
                {
                    uint32_t h=m_base,w=0x20000;
                    for(unsigned i=0;i<48;++i)
                    {
                        if(m_banks[i].loaded)
                        {
                            if(h!=m_headerBases[i] || read32(input.data()+i*4)<m_banks[i].headerSize)
                            {result=-2;break;}
                            if(w!=m_waveBases[i] || read32(input.data()+192+i*4)<m_banks[i].waveSize)
                            {result=-6;break;}
                        }
                        h+=read32(input.data()+i*4);w+=read32(input.data()+192+i*4);
                    }
                }
                if (!result)
                {
                    uint32_t h=m_base, w=0x20000;
                    for (unsigned i=0;i<48;++i)
                    {
                        m_headerBases[i]=h; m_waveBases[i]=w;
                        m_headerCaps[i]=read32(input.data()+i*4);
                        m_waveCaps[i]=read32(input.data()+192+i*4);
                        h+=m_headerCaps[i]; w+=m_waveCaps[i];
                    }
                    m_ready=true;
                }
            }
            else if (r.function == 0xfe00)
            {
                for (unsigned i=0;i<48;++i)
                {
                    write32(reply.data()+i*4,m_headerCaps[i]);
                    write32(reply.data()+192+i*4,m_waveCaps[i]);
                }
            }
            if (r.function != 0xfe00) write32(reply.data(),static_cast<uint32_t>(result));
            if (!m_host.writeGuest(r.receive.address,reply.data(),outputBytes)) return {};
            RpcResult done;
            done.handled=true; done.resultAddress=r.receive.address;
            done.signalNowaitCompletion=true; done.serverDispatchPolicy=ServerDispatchPolicy::Suppress;
            return done;
        }
        void metrics(std::vector<DebugMetric>& out) const
        {
            unsigned playing=0,prepared=0;for(const auto& v:m_voices){playing+=v.playing;prepared+=v.prepared;}
            out.push_back({"sndf_voices_playing",playing,false});
            out.push_back({"sndf_voices_prepared",prepared,false});
            out.push_back({"sndf_pcm_submitted",m_submitted,false});
            out.push_back({"sndf_pcm_consumed",m_pcmKey?m_host.pcmConsumed(m_pcmKey):0,false});
            out.push_back({"sndf_voices_completed",m_completed,false});
            out.push_back({"sndf_pitch_base_bits",m_flags,true});
            unsigned banks=0,tones=0;
            for(const auto& b:m_banks)banks+=b.loaded;
            for(const auto& t:m_tones)tones+=t[0]!=0;
            out.push_back({"sndf_registered_banks",banks,false});
            out.push_back({"sndf_initialized_tones",tones,false});
            out.push_back({"sndf_wave_count",m_totalWaves,false});
            uint64_t toneHash=14695981039346656037ull;
            for(const auto& t:m_tones)for(auto b:t)toneHash=(toneHash^b)*1099511628211ull;
            out.push_back({"sndf_tone_fingerprint",toneHash,true});
            out.push_back({"sndf_initialized",m_initialized,false});
            out.push_back({"sndf_work_base",m_base,true});
            out.push_back({"sndf_work_size",m_size,true});
            out.push_back({"sndf_command_queue",m_queue,true});
            out.push_back({"sndf_status_address",m_statusAddress,true});
            out.push_back({"sndf_layout_ready",m_ready,false});
            out.push_back({"sndf_header_base_0",m_headerBases[0],true});
            out.push_back({"sndf_header_base_1",m_headerBases[1],true});
            out.push_back({"sndf_wave_base_0",m_waveBases[0],true});
            out.push_back({"sndf_wave_base_1",m_waveBases[1],true});
        }
        unsigned outputMode() const {return m_outputMode;}
        // Applies an entire staged batch only after a writable RPC reply and a
        // usable PCM sink have been established. Busy retains the EE's batch.
        bool updateSound(std::span<const uint8_t> packet,unsigned count,uint32_t receive,bool& accepted)
        {
            pumpAudio();
            std::array<uint8_t,0x1c0> reply{};
            if(!m_host.readGuest(receive,reply.data(),reply.size()))return false;
            reply.fill(0);
            auto voices=m_voices;auto status=m_status;unsigned mode=m_outputMode;
            accepted=true;bool starts=false;
            for(unsigned i=0;i<count;++i)
                if(!stageSound(packet.subspan(i*20,20),voices,status,mode,starts)){accepted=false;break;}
            if(accepted && starts && !m_pcmKey){
                const auto key=m_host.allocateIopHandle(IopHandleKind::RpcPacket);
                if(!key || !m_host.pcmConfigure(key,48000,true)){if(key)m_host.pcmClose(key);accepted=false;}
                else m_pcmKey=key;
            }
            reply[0]=accepted?0:1;
            if(!m_host.writeGuest(receive,reply.data(),reply.size()))return false;
            if(accepted){m_voices=std::move(voices);m_status=status;m_outputMode=mode;pumpAudio();}
            if(m_statusAddress)m_host.writeGuest(m_statusAddress,m_status.data(),m_status.size());
            return true;
        }
    private:
        #include "mk_sndf_playback.inc"
        static uint32_t read32(const uint8_t* p)
        { return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24); }
        static void write32(uint8_t* p,uint32_t x)
        { for(unsigned i=0;i<4;++i)p[i]=static_cast<uint8_t>(x>>(i*8)); }
        void resetStatus()
        {
            m_banks.fill(Bank{});m_totalWaves=0;
            for(auto& tone:m_tones){tone.fill(0);tone[15]=0x40;}
            m_status.fill(0);
            for (unsigned i=0;i<48;++i)
            {
                auto* p=m_status.data()+i*16;
                p[0x14]=0xff; p[0x16]=p[0x17]=p[0x1a]=0x7f;
                p[0x18]=p[0x19]=p[0x1b]=0x40;
                m_status[0x25cf+i*4]=0x7f;
            }
            std::fill(m_status.begin()+0x30c,m_status.begin()+0x1b0c,0xff);
        }
        void release()
        {
            if (m_ownsBase && m_base) m_host.freeIopMemory(m_base);
            if (m_queue) m_host.freeIopMemory(m_queue);
            m_base=m_queue=0; m_ownsBase=false;
        }
        // e1d0 -> 7a18 -> 7e4c/8234/80e4: bank metadata and initial tones.
        int32_t registerBank(unsigned slot)
        {
            if(!m_ready)return -4;
            if(m_banks[slot].id!=UINT32_MAX)return -3;
            if(m_banks[slot].loaded)return -6;
            if(m_headerCaps[slot]<0xc4)return -8;
            std::vector<uint8_t> data(m_headerCaps[slot]);
            if(!m_host.readGuest(m_headerBases[slot],data.data(),data.size()))return -8;
            if(read32(data.data())!=0x5f327370 || read32(data.data()+4)!=0x4b505444)return -8;
            if(read16(data.data()+8)<0x4702)return -7;
            const unsigned waves=data[0x89];
            if(m_totalWaves+waves>512)return -5;
            Bank bank;
            bank.id=read32(data.data()+0x84);bank.headerSize=read32(data.data()+0x10);
            bank.waveSize=read32(data.data()+0x18);bank.waveCount=waves;
            if(bank.headerSize>data.size() || bank.waveSize>m_waveCaps[slot])return -8;
            bank.aliasSlot=slot;
            const auto alias=read32(data.data()+0x20);
            if(alias&0x80000000u)
            {
                unsigned other=0;
                for(;other<48;++other)if(m_banks[other].loaded && m_banks[other].id==(alias&0x7fffffff))break;
                if(other==48)return -8; // Reject a dangling alias before reading host memory.
                bank.aliasSlot=other;
            }
            const auto& waveHeader=bank.aliasSlot==slot?data:m_banks[bank.aliasSlot].header;
            auto status=m_status;
            auto tones=m_tones;
            auto* statusBank=status.data()+slot*16;
            statusBank[0x14]=0xff;statusBank[0x15]=0x7f;
            const unsigned initial=read16(data.data()+0xc2);
            write32(statusBank+0x10,0);
            if(initial)
            {
                const uint64_t program=uint64_t(read32(data.data()+0x98))+initial;
                if(program+8>data.size())return -8;
                write32(statusBank+0x10,m_headerBases[slot]+uint32_t(program));
                const unsigned count=unsigned(data[size_t(program)])+1;
                if(program+8+count*16>data.size())return -8;
                if(!(data[size_t(program)+1]&0x80))statusBank[0x17]=data[size_t(program)+1];
                for(unsigned n=0;n<count;++n)
                {
                    const auto offset=size_t(program)+8+n*16;
                    const auto* p=data.data()+offset;
                    if(!(p[0]&0x80))continue;
                    if(p[1]>=16)return -8;
                    size_t index=0;while(index<tones.size() && tones[index][0])++index;
                    if(index==tones.size())return -5;
                    auto& tone=tones[index];tone[0]=1;tone[1]=p[0];tone[3]=p[1];
                    tone[4]=p[2];tone[5]=p[3];
                    for(unsigned k=6;k<12;++k)tone[k]=p[k];
                    tone[12]=p[10];tone[13]=p[11];
                    const bool sequence=(p[0]&2)!=0;
                    const int volume=statusBank[sequence?0x17:0x16];
                    const int pan=int(static_cast<int8_t>(statusBank[sequence?0x19:0x18]));
                    tone[14]=static_cast<uint8_t>(std::clamp(volume+(pan-64)*2,0,127));
                    write32(tone.data()+20,m_headerBases[slot]+uint32_t(offset));
                    uint64_t instrument=0;
                    if(sequence)instrument=read32(waveHeader.data()+0xa8);
                    else
                    {
                        const uint64_t table=read32(waveHeader.data()+0x9c);
                        const uint64_t first=table+(unsigned(p[2])+1)*2;
                        if(first+2>waveHeader.size())return -8;
                        const uint64_t second=table+read16(waveHeader.data()+size_t(first));
                        const uint64_t variation=second+(unsigned(p[3])+1)*2;
                        if(variation+2>waveHeader.size())return -8;
                        const auto value=read16(waveHeader.data()+size_t(variation));
                        if(value)instrument=second+value;
                    }
                    if(instrument>=waveHeader.size())return -8;
                    write32(tone.data()+24,instrument?m_headerBases[bank.aliasSlot]+uint32_t(instrument):0);
                    auto* channels=status.data()+0x30c+slot*0x80+p[1]*8;
                    for(unsigned c=0;c<4;++c)if(read16(channels+c*2)==0xffff)
                    {channels[c*2]=uint8_t(index);channels[c*2+1]=uint8_t(index>>8);break;}
                }
            }
            if(m_statusAddress && !m_host.writeGuest(m_statusAddress,status.data(),status.size()))return -8;
            std::vector<uint8_t> uploaded(bank.waveSize);
            bank.waveUploaded=m_host.readSoundMemory(m_waveBases[slot],uploaded.data(),uploaded.size());
            if(bank.waveUploaded)
            {
                bank.waveHash=14695981039346656037ull;
                for(auto b:uploaded)bank.waveHash=(bank.waveHash^b)*1099511628211ull;
            }
            bank.header=std::move(data);bank.loaded=true;
            m_banks[slot]=std::move(bank);m_totalWaves+=waves;
            m_status=status;m_tones=tones;
            std::ostringstream text;
            text<<"[MK SNDF] registered bank="<<slot<<" id=0x"<<std::hex<<m_banks[slot].id
                <<" header=0x"<<m_headerBases[slot]<<" waves="<<std::dec<<waves
                <<" uploaded="<<m_banks[slot].waveUploaded<<" wave_bytes="<<m_banks[slot].waveSize
                <<" wave_hash="<<std::hex<<m_banks[slot].waveHash;
            m_host.log(LogLevel::Info,text.str());
            return 0;
        }
        static uint16_t read16(const uint8_t* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
        struct Bank
        {
            uint32_t id=UINT32_MAX,headerSize=0,waveSize=0;
            unsigned waveCount=0,aliasSlot=0;
            bool loaded=false,waveUploaded=false;
            uint64_t waveHash=0;
            std::vector<uint8_t> header;
        };
        std::array<Bank,48> m_banks{};
        std::array<std::array<uint8_t,28>,512> m_tones{};
        unsigned m_totalWaves=0;
        IopHost& m_host;
        uint32_t m_size=0,m_external=0,m_base=0,m_queue=0,m_statusAddress=0;
        uint32_t m_spuSize=0x1c7fc0,m_rate=0,m_flags=0;
        bool m_ownsBase=false,m_initialized=false,m_ready=false;
        std::array<uint32_t,48> m_headerCaps{},m_waveCaps{},m_headerBases{},m_waveBases{};
        std::array<uint8_t,0x2b00> m_status{};
    };
}
