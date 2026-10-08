#pragma once
#include "../../../ps2xIOP/src/modules/cri_descriptor_queue.h"
#include "runtime/ps2_pcm_queue.h"
#include "ps2_host_backend.h"
#include <chrono>
#include <thread>
static bool checkPcmDevice() {
    InitAudioDevice();if(!IsAudioDeviceReady())return false;
    PS2AudioBackend audio;audio.setAudioReady(true);
    constexpr uint32_t key=0x435249;
    std::array<int16_t,2048> silence{};
    bool ok=audio.pcmConfigure(key,48000,true) && audio.pcmSubmit(key,silence);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(ok && audio.pcmConsumed(key)<1024 && std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ok=ok && audio.pcmConsumed(key)==1024;
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    ok=ok && audio.pcmConsumed(key)==1024; // device keeps pulling silent underruns
    audio.pcmConfigure(key,48000,false);
    ok=ok && audio.pcmSubmit(key,silence);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    ok=ok && audio.pcmConsumed(key)==1024;
    audio.pcmConfigure(key,48000,true);
    const auto resumed=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(ok && audio.pcmConsumed(key)<2048 && std::chrono::steady_clock::now()<resumed)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ok=ok && audio.pcmConsumed(key)==2048;
    audio.pcmClose(key);ok=ok && audio.pcmConsumed(key)==0;
    audio.stopAll();audio.setAudioReady(false);CloseAudioDevice();return ok;
}
static bool checkPcmQueue() {
    PS2PcmQueue q;std::vector<int16_t> input(PS2PcmQueue::Capacity*2),output(input.size());
    for(size_t i=0;i<input.size();++i)input[i]=static_cast<int16_t>(i*31);
    q.pull(output);if(q.consumed()!=0 || output!=std::vector<int16_t>(output.size()))return false;
    if(!q.push(input) || q.push(std::span(input.data(),2)))return false;
    q.pull(std::span(output.data(),3000));
    if(!std::equal(output.begin(),output.begin()+3000,input.begin()) || q.consumed()!=1500)return false;
    if(!q.push(std::span(input.data(),3000)))return false;
    q.pull(output);
    for(size_t i=0;i<output.size();++i)if(output[i]!=input[(i+3000)%input.size()])return false;
    if(q.consumed()!=PS2PcmQueue::Capacity+1500)return false;
    q.pull(output);
    return q.consumed()==PS2PcmQueue::Capacity+1500 && output==std::vector<int16_t>(output.size());
}
static bool checkCriDescriptorQueue() {
    using namespace ps2x::iop::detail;
    struct Case {uint32_t mode, op, line, a, b, result[2], bytes[4], sizes[4]; CriChunk chunks[3];};
    const Case cases[] = {
#include "cri_queue_cases.inc"
    };
    CriDescriptorQueue queue(3,0);
    uint32_t mode=0;
    for(const auto& c : cases) {
        if(c.mode!=mode) {mode=c.mode;queue=CriDescriptorQueue(3,static_cast<uint8_t>(mode));}
        CriChunk result{};
        if(c.op<2) queue.put(c.line,{c.a,c.b},c.op==1);
        else if(c.op==2) result=queue.get(c.line,c.a);
        else {const auto [ready,size]=queue.canGet(c.line,c.a);result={ready?1u:0u,size};}
        if(result!=CriChunk{c.result[0],c.result[1]}) return false;
        unsigned index=0;
        for(unsigned line=0;line<4;++line) {
            if(queue.bytes(line)!=c.bytes[line] || queue.chunks(line).size()!=c.sizes[line])return false;
            for(auto chunk:queue.chunks(line)) if(chunk!=c.chunks[index++])return false;
        }
        if(queue.usedDescriptors()!=index)return false;
    }
    queue.reset();
    return queue.usedDescriptors()==0 && !queue.put(4,{1,1}) &&
           !queue.put(0,{0xfffffff0,32}) && queue.get(4,1)==CriChunk{};
}

#include "../../../ps2xRuntime/src/lib/ps2_iop_transport.h"
static bool checkCriSurvivesRpcInit() {
    auto runtime=std::make_unique<PS2Runtime>();
    std::vector<uint8_t> ram(PS2_RAM_SIZE);R5900Context context{};
    if(!PS2IopTransport::configureForTesting(runtime.get(),{"SLUS_210.87",0x11c070,0x1a37a67c}))return false;
    ps2_syscalls::SifInitRpc(ram.data(),&context,runtime.get());
    const uint32_t packet[]{1,0x4000,0x100};std::memcpy(ram.data()+0x1000,packet,sizeof(packet));
    ps2x::iop::RpcRequest request{};request.sid=0x90000200;request.function=0x422;
    request.send={0x1000,sizeof(packet)};request.receive={0x1100,4};
    if(!PS2IopTransport::handleRpc(runtime.get(),ram.data(),&context,request).handled)return false;
    uint32_t handle;std::memcpy(&handle,ram.data()+0x1100,4);
    ps2_syscalls::SifInitRpc(ram.data(),&context,runtime.get());
    const uint32_t query[]{handle,1};std::memcpy(ram.data()+0x1000,query,sizeof(query));
    request.function=0x429;request.send.size=sizeof(query);
    if(!PS2IopTransport::handleRpc(runtime.get(),ram.data(),&context,request).handled)return false;
    PS2IopTransport::reset(runtime.get());
    return !PS2IopTransport::handleRpc(runtime.get(),ram.data(),&context,request).handled;
}
