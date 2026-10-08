#include "../module_factories.h"
#include "cri_descriptor_queue.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

namespace ps2x::iop::detail {
namespace {
constexpr uint32_t Sid = 0x90000200u;
struct Bridge {uint32_t source, destination, returnLine, eeObject; uint16_t xid = 0;};
struct PcmBatch {uint64_t end;std::array<CriChunk,2> chunks;std::array<uint32_t,2> queues;};
struct Rna {
    uint32_t channels, core, sj[2], rate, volume = 255;
    bool playing = false;uint8_t transferFlag = 0;std::array<int32_t,2> pan{};
    uint64_t submitted=0,returned=0;std::deque<PcmBatch> pending;
};
struct Transport {uint32_t id, ee, iop, size, remote;};
class MkCri final : public IopService {
public:
    explicit MkCri(IopHost& host) : host_(host), transport_(createCriDtxService(host,
        {.serviceName="MK CRI DTX transport", .sid=Sid, .enableUrpc=false})) {}
    ~MkCri() override {for(const auto& [h,r]:rna_)host_.pcmClose(h);}
    std::string_view name() const override {return "Shaolin Monks CRI ADX";}
    std::span<const uint32_t> sids() const override {return sids_;}
    RpcAbi selectRpcAbi(const RpcAbiRequest& r) const override {return transport_->selectRpcAbi(r);}
    void reset() override {
        std::lock_guard lock(mutex_);for(const auto& [h,r]:rna_)host_.pcmClose(h);
        transport_->reset();queues_.clear();bridges_.clear();rna_.clear();transfers_.clear();
        calls_=batches_=held_=0;rate_=48000;coreRunning_={};voiceSetup_={};
    }
    RpcResult handleRpc(const RpcRequest& r) override {
        RpcResult result{};
        if(r.sid!=Sid)return result;
        result.serverDispatchPolicy=ServerDispatchPolicy::Suppress;
        std::lock_guard lock(mutex_);
        if(r.function<0x400 || r.function>=0x500) {
            std::array<uint32_t,4> w{};
            if(r.function==2 && (!read(r,w,4) || w[3]<64 || w[3]>0x10000))return result;
            result=transport_->handleRpc(r);
            if(result.handled && r.function==2) {
                uint32_t handle=0;host_.readGuest(r.receive.address,&handle,4);
                transfers_[w[0]]={w[0],w[1],w[2],w[3],handle};
            }
            if(result.handled && r.function==3 && read(r,w,1)) {
                for(auto it=transfers_.begin();it!=transfers_.end();)
                    if(it->second.remote==w[0])it=transfers_.erase(it);else ++it;
            }
            return result;
        }
        std::array<uint32_t,4> w{};
        const uint32_t command=r.function&255;
        auto success=[&] {result.handled=true;result.resultAddress=r.receive.address;++calls_;return result;};
        auto finish=[&](std::span<const uint32_t> out) {return reply(r,out) ? success() : result;};
        if(command==3 && r.send.size==0)return finish({}); // SJX initialization: no payload/reply.
        if(command==0x22) {
            if(!read(r,w,3) || w[2]<16 || w[2]>0x100000 || !range(w[1],w[2]) || queues_.size()>=64 || r.receive.size<4)return result;
            const uint32_t h=allocateHandle();
            if(!h || !reply(r,std::span(&h,1)))return result;
            queues_.emplace(h,CriDescriptorQueue(w[2]/16,static_cast<uint8_t>(w[0])));
            return success();
        }
        if(command==0) {
            if(!read(r,w,4) || !queues_.contains(w[1]) || w[2]>1 || !w[3] || bridges_.size()>=32 || r.receive.size<4)return result;
            const uint32_t h=allocateHandle();
            if(!h || !reply(r,std::span(&h,1)))return result;
            bridges_.emplace(h,Bridge{w[0],w[1],w[2]==1 ? 0u:1u,w[3]});return success();
        }
        if(command==1 || command==2) {
            if(!read(r,w,command==1 ? 1:2))return result;
            auto it=bridges_.find(w[0]);if(it==bridges_.end() || !reply(r,std::span(w.data(),1)))return result;
            if(command==1)bridges_.erase(it);else it->second.xid=static_cast<uint16_t>(w[1]);
            return success();
        }
        if(command==8) {
            if(!read(r,w,4) || w[0]<1 || w[0]>2 || w[1]>1 || !queues_.contains(w[2]) ||
               (w[0]==2 && !queues_.contains(w[3])) || rna_.size()>=16 || r.receive.size<4)return result;
            const uint32_t h=allocateHandle();
            if(!h || !reply(r,std::span(&h,1)))return result;
            rna_.emplace(h,Rna{w[0],w[1],{w[2],w[0]==2 ? w[3]:0},rate_});return success();
        }
        if(command==9) {
            if(!read(r,w,1))return result;
            const auto voice=rna_.find(w[0]);if(voice==rna_.end())return result;
            // Original RCV 0x9ff4 returns source chunks after copying into its
            // own output buffer. Our sink retains them until playback consumes
            // them. Cancel that extra ownership on destroy, or the next EE RNA
            // worker (0x423d50) waits forever for its full 16 KiB room queue.
            // Stage all returns before publishing the RPC or closing the sink.
            auto queues=queues_;
            for(const auto& pending:voice->second.pending)
                for(unsigned c=0;c<2;++c)if(pending.chunks[c].size) {
                    const auto q=queues.find(pending.queues[c]);
                    if(q==queues.end() || !q->second.put(0,pending.chunks[c]))return result;
                }
            if(!reply(r,std::span(w.data(),1)))return result;
            host_.pcmClose(w[0]); // stop device reads before exposing room
            queues_=std::move(queues);rna_.erase(voice);return success();
        }
        if(command==10) {
            if(!read(r,w,3) || !reply(r,std::span(w.data(),1)))return result;
            voiceSetup_={w[0],w[1],w[2]};return success();
        }
        if(command==12) {
            if(!read(r,w,1) || w[0]==0 || w[0]>192000)return result;
            constexpr uint32_t core=0;
            if(!reply(r,std::span(&core,1)))return result;
            rate_=w[0];return success();
        }
        if(command==16 || command==17) {
            if(!read(r,w,1) || w[0]>1 || !reply(r,std::span(w.data(),1)))return result;
            coreRunning_[w[0]]=command==16;return success();
        }
        if(command>=0x23 && command<=0x2a && command!=0x24) {
            const unsigned count=(command==0x23 || command==0x25) ? 1 : command==0x29 ? 2 :
                                 (command==0x27 || command==0x28) ? 4 : 3;
            if(!read(r,w,count))return result;
            auto it=queues_.find(w[0]);if(it==queues_.end())return result;
            auto queue=it->second;std::array<uint32_t,2> out{w[0],w[1]};unsigned words=1;
            if(command==0x25)queue.reset();
            else if(command==0x26) {auto c=queue.get(w[1],w[2]);out={c.address,c.size};words=2;}
            else if(command==0x27 || command==0x28) {if(!queue.put(w[1],{w[2],w[3]},command==0x27))return result;}
            else if(command==0x29)out[0]=queue.bytes(w[1]);
            else if(command==0x2a) {auto [yes,size]=queue.canGet(w[1],w[2]);out={yes?1u:0u,size};words=2;}
            if(!reply(r,std::span(out.data(),words)))return result;
            if(command==0x23)queues_.erase(it);else it->second=std::move(queue);
            return success();
        }
        return result;
    }
    void onSifTransfer(const SifTransfer& t) override {
        if(t.kind!=SifTransferKind::SetDma || t.phase!=SifTransferPhase::AfterCopy)return;
        std::lock_guard lock(mutex_);
        uint32_t source=0; if(!host_.normalizeGuestAddress(t.sourceAddress,source))return;
        for(const auto& [id,transfer] : transfers_) {
            uint32_t ee=0;if(!host_.normalizeGuestAddress(transfer.ee,ee) || source!=ee || t.size!=transfer.size)continue;
            if(id>1) {++held_;return;}
            std::vector<uint8_t> bytes(t.size);
            if(!host_.readGuest(source,bytes.data(),bytes.size()))return;
            auto word=[&](size_t offset){uint32_t value;std::memcpy(&value,bytes.data()+offset,4);return value;};
            const uint32_t count=word(0);
            if(count>128 || 16ull+uint64_t(count)*16+4>bytes.size()) {++held_;return;}
            auto queues=queues_;auto rna=rna_;
            for(uint32_t i=0;i<count;++i) {
                const size_t off=16+i*16;const auto h=word(off+4),a=word(off+8),b=word(off+12);
                if(id==0) {
                    if(bytes[off]!=0) {++held_;return;}
                    auto it=bridges_.find(h);if(it==bridges_.end()) {++held_;return;}
                    auto q=queues.find(it->second.destination);
                    if(q==queues.end() || !q->second.put(1,{a,b})) {++held_;return;}
                } else {
                    const uint32_t command=word(off)&65535;
                    auto it=rna.find(h);if(it==rna.end()) {++held_;return;}
                    auto& state=it->second;
                    switch(command) {
                    case 0: state.playing=true;break;
                    case 1: state.playing=false;break;
                    case 2: state.playing=a==1;break;
                    case 3: if(a>2) {++held_;return;} state.channels=a;break;
                    case 4: if(!a || a>192000) {++held_;return;} state.rate=a;break;
                    case 5: state.volume=static_cast<uint32_t>(std::clamp(std::bit_cast<int32_t>(b),0,255));break;
                    case 8: state.transferFlag=static_cast<uint8_t>(a);break;
                    // IOP 0x29d0: signed pan clamped to [-15,15], one channel.
                    case 9:
                        if(a>=state.pan.size()) {++held_;return;}
                        state.pan[a]=std::clamp(std::bit_cast<int32_t>(b),-15,15);break;
                    default: ++held_;return;
                    }
                }
            }
            // Only device-consumed PCM may appear in the return queue. Pumping
            // after the transaction commits makes those returns visible next DMA.
            uint32_t outputCount=0;
            // Original producer changes count and output records only; preserve
            // reserved header fields, unused records and the transport footer.
            if(id==0) for(const auto& [h,bridge]:bridges_) {
                auto q=queues.find(bridge.destination);if(q==queues.end())continue;
                while(outputCount<128 && 16ull+(uint64_t(outputCount)+1)*16+4<=bytes.size()) {
                    auto chunk=q->second.get(bridge.returnLine,0x7fffffff);if(!chunk.size)break;
                    const size_t off=16+outputCount++*16;
                    bytes[off]=0;
                    bytes[off+1]=static_cast<uint8_t>(bridge.returnLine);
                    std::memcpy(bytes.data()+off+2,&bridge.xid,2);
                    std::memcpy(bytes.data()+off+4,&bridge.eeObject,4);
                    std::memcpy(bytes.data()+off+8,&chunk.address,4);
                    std::memcpy(bytes.data()+off+12,&chunk.size,4);
                }
            }
            std::memcpy(bytes.data(),&outputCount,4);
            if(!host_.writeGuest(source,bytes.data(),bytes.size()))return;
            queues_=std::move(queues);rna_=std::move(rna);++batches_;pumpAudio();
            transport_->onSifTransfer(t);return;
        }
    }
    void appendDebugMetrics(std::vector<DebugMetric>& out) const override {
        std::lock_guard lock(mutex_);transport_->appendDebugMetrics(out);
        uint64_t active=0;for(const auto& [h,r]:rna_)active+=r.playing;
        out.push_back({"playing_rna_objects",active,false});
        unsigned voiceIndex=0;
        for(const auto& [h,r]:rna_){
            const auto prefix="rna_"+std::to_string(voiceIndex++)+"_";
            out.push_back({prefix+"handle",h,true});
            out.push_back({prefix+"playing",r.playing,false});
            out.push_back({prefix+"channels",r.channels,false});
            out.push_back({prefix+"rate",r.rate,false});
            out.push_back({prefix+"submitted",r.submitted,false});
            out.push_back({prefix+"consumed",host_.pcmConsumed(h),false});
            out.push_back({prefix+"returned",r.returned,false});
            for(unsigned c=0;c<2;++c){
                const auto q=queues_.find(r.sj[c]);
                out.push_back({prefix+"queue_"+std::to_string(c),r.sj[c],true});
                out.push_back({prefix+"bytes_"+std::to_string(c),q==queues_.end()?0:q->second.bytes(1),false});
            }
        }
        uint64_t submitted=0,consumed=0,pending=0;
        for(const auto& [h,r]:rna_) {submitted+=r.submitted;consumed+=host_.pcmConsumed(h);pending+=r.pending.size();}
        out.push_back({"pcm_submitted_frames",submitted,false});out.push_back({"pcm_consumed_frames",consumed,false});
        out.push_back({"pcm_pending_chunks",pending,false});
        uint64_t data=0,room=0;for(const auto& [h,q]:queues_) {data+=q.bytes(1);room+=q.bytes(0);}
        out.push_back({"uni_objects",queues_.size(),false});out.push_back({"sjx_objects",bridges_.size(),false});
        out.push_back({"rna_objects",rna_.size(),false});out.push_back({"protocol_calls",calls_,false});
        out.push_back({"descriptor_batches",batches_,false});out.push_back({"held_batches",held_,false});
        out.push_back({"queued_pcm_bytes",data,false});out.push_back({"returned_pcm_bytes",room,false});
    }
private:
    void pumpAudio() {
        // IOP 0xa27c copies equal-rate 16-bit PCM unchanged. Host raylib handles
        // rate conversion; source-frame counters track only copied guest samples.
        static constexpr int panTable[31]={255,254,253,251,249,246,242,238,232,227,220,213,206,198,189,180,
            170,160,149,138,127,115,103,91,78,65,53,39,26,13,0};
        for(auto& [h,r]:rna_) {
            if(!host_.pcmConfigure(h,r.rate,r.playing))continue;
            const auto consumed=host_.pcmConsumed(h);
            while(!r.pending.empty() && r.pending.front().end<=consumed) {
                const auto& done=r.pending.front();auto staged=queues_;bool valid=true;
                for(unsigned c=0;c<2;++c) if(done.chunks[c].size) {
                    auto q=staged.find(done.queues[c]);
                    if(q==staged.end() || !q->second.put(0,done.chunks[c])) {valid=false;break;}
                }
                if(!valid)break;
                queues_=std::move(staged);r.returned=done.end;r.pending.pop_front();
            }
            if(!r.playing || r.channels<1 || r.channels>2)continue;
            // At most 2,048 frames in flight per voice. Preserve both channels if
            // either source is empty, invalid or the device queue applies pressure.
            while(r.pending.size()<8) {
                uint32_t frames=256;
                for(unsigned c=0;c<r.channels;++c) {
                    const auto q=queues_.find(r.sj[c]);
                    if(q==queues_.end() || q->second.chunks(1).empty()) {frames=0;break;}
                    frames=std::min(frames,q->second.chunks(1).front().size/2);
                }
                // PSMSLT_Create sets RCV's minimum to 32 samples; the equal-rate
                // converter copies 32-sample blocks (0xa27c). Keep short tails.
                frames&=~31u;
                if(!frames)break;
                auto staged=queues_;PcmBatch batch{r.submitted+frames,{}, {r.sj[0],r.sj[1]}};
                std::array<std::array<uint8_t,512>,2> source{};bool valid=true;
                for(unsigned c=0;c<r.channels;++c) {
                    auto& q=staged.at(r.sj[c]);batch.chunks[c]=q.get(1,frames*2);
                    if(batch.chunks[c].size!=frames*2 || !host_.readGuest(batch.chunks[c].address,source[c].data(),frames*2)) {valid=false;break;}
                }
                if(!valid)break;
                std::array<int16_t,512> stereo{};
                for(unsigned f=0;f<frames;++f) {
                    int mixed[2]{};
                    for(unsigned c=0;c<r.channels;++c) {
                        const uint16_t bits=source[c][f*2] | (uint16_t(source[c][f*2+1])<<8);
                        const auto sample=std::bit_cast<int16_t>(bits);
                        for(unsigned side=0;side<2;++side) {
                            const int gain=(int(r.volume)*panTable[15+(side?-r.pan[c]:r.pan[c])]+127)/255;
                            mixed[side]+=int(sample)*gain/255;
                        }
                    }
                    stereo[f*2]=static_cast<int16_t>(std::clamp(mixed[0],-32768,32767));
                    stereo[f*2+1]=static_cast<int16_t>(std::clamp(mixed[1],-32768,32767));
                }
                if(!host_.pcmSubmit(h,std::span(stereo.data(),frames*2)))break;
                queues_=std::move(staged);r.submitted=batch.end;r.pending.push_back(batch);
            }
        }
    }
    uint32_t allocateHandle() {
        // The shared packet allocator wraps. Do not alias a still-live object.
        for(unsigned i=0;i<1024;++i) {
            const uint32_t h=host_.allocateIopHandle(IopHandleKind::RpcPacket);
            if(!h)return 0;
            if(!queues_.contains(h) && !bridges_.contains(h) && !rna_.contains(h))return h;
        }
        return 0;
    }
    bool read(const RpcRequest& r,std::array<uint32_t,4>& out,unsigned n) const {
        return r.send.size>=n*4 && r.send.address && host_.readGuest(r.send.address,out.data(),n*4);
    }
    bool range(uint32_t address,uint32_t size) const {
        uint8_t value;
        return address && size && address<=UINT32_MAX-size && host_.readGuest(address,&value,1) &&
               host_.readGuest(address+size-1,&value,1);
    }
    bool reply(const RpcRequest& r,std::span<const uint32_t> words) {
        if(!r.receive.size)return true;
        if(!r.receive.address || r.receive.size>256 || r.receive.size>words.size_bytes())return false;
        return host_.writeGuest(r.receive.address,words.data(),r.receive.size);
    }
    IopHost& host_;std::unique_ptr<IopService> transport_;mutable std::mutex mutex_;
    std::map<uint32_t,CriDescriptorQueue> queues_;std::map<uint32_t,Bridge> bridges_;
    std::map<uint32_t,Rna> rna_;std::map<uint32_t,Transport> transfers_;
    uint64_t calls_=0,batches_=0,held_=0;uint32_t rate_=48000;
    std::array<bool,2> coreRunning_{};std::array<uint32_t,3> voiceSetup_{};
    const std::array<uint32_t,1> sids_{Sid};
};
}
std::unique_ptr<IopService> createMkCriService(IopHost& host) {return std::make_unique<MkCri>(host);}
}
