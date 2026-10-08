#include "MiniTest.h"
#include "ps2x/iop/iop_subsystem.h"
#include "../../ps2xIOP/src/modules/mk_sndf_voice.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__linux__)
#include <dlfcn.h>
#endif

namespace
{
    using namespace ps2x::iop;

    constexpr uint32_t kSyntheticSid = 0xF00DCAFEu;
    constexpr uint32_t kCoreCollisionSid = 0x80001300u;
    constexpr uint32_t kSyntheticFunction = 0x42u;
    constexpr uint32_t kCoreCollisionFunction = 0x99u;
    constexpr uint32_t kSyntheticEntryPoint = 0x00123456u;
    constexpr uint32_t kSpecificRecvXEntryPoint = kSyntheticEntryPoint + 0x100u;
    constexpr uint32_t kSyntheticCrc32 = 0xA1B2C3D4u;
    constexpr uint32_t kResponseXor = 0xA5A55A5Au;
    constexpr uint32_t kCoreCollisionResponse = 0xC0DEF00Du;

    class FakeIopHost final : public IopHost
    {
    public:
        explicit FakeIopHost(size_t memorySize = 0x10000u)
            : memory(memorySize, 0u)
        {
        }

        struct Pcm {uint32_t rate=0;bool playing=false;uint64_t consumed=0;std::vector<int16_t> samples;};
        bool enablePcm=false,acceptPcm=true;
        std::unordered_map<uint32_t,Pcm> pcm;
        bool pcmConfigure(uint32_t key,uint32_t rate,bool playing) override {
            if(!enablePcm)return false;
            auto& p=pcm[key];p.rate=rate;p.playing=playing;return true;
        }
        bool pcmSubmit(uint32_t key,std::span<const int16_t> stereo) override {
            if(!acceptPcm)return false;
            auto& p=pcm.at(key);p.samples.insert(p.samples.end(),stereo.begin(),stereo.end());return true;
        }
        uint64_t pcmConsumed(uint32_t key) const override {
            const auto it=pcm.find(key);return it==pcm.end()?0:it->second.consumed;
        }
        void pcmClose(uint32_t key) override {pcm.erase(key);}

        std::vector<uint8_t> soundMemory;
        bool readSoundMemory(uint32_t address, void* data, size_t size) const override {
            if(address>soundMemory.size() || size>soundMemory.size()-address)return false;
            std::memcpy(data,soundMemory.data()+address,size);return true;
        }

        bool readGuest(uint32_t address, void *destination, size_t size) const override
        {
            if ((!destination && size != 0u) || !contains(address, size))
            {
                return false;
            }
            if (size != 0u)
            {
                std::memcpy(destination, memory.data() + address, size);
            }
            return true;
        }

        bool writeGuest(uint32_t address, const void *source, size_t size) override
        {
            if ((!source && size != 0u) || !contains(address, size))
            {
                return false;
            }
            if (size != 0u)
            {
                std::memcpy(memory.data() + address, source, size);
            }
            return true;
        }

        bool zeroGuest(uint32_t address, size_t size) override
        {
            if (!contains(address, size))
            {
                return false;
            }
            std::fill(memory.begin() + address, memory.begin() + address + size, 0u);
            return true;
        }

        bool normalizeGuestAddress(uint32_t address, uint32_t &normalized) const override
        {
            normalized = address & 0x1FFFFFFFu;
            return normalized < memory.size();
        }

        uint32_t allocateIopHandle(IopHandleKind kind) override
        {
            const uint32_t value = nextHandle;
            nextHandle += (kind == IopHandleKind::RpcPacket) ? 0x40u : 0x80u;
            return value;
        }

        uint32_t allocateGuest(uint32_t size, uint32_t alignment) override
        {
            if (size == 0u)
            {
                return 0u;
            }
            const uint64_t effectiveAlignment = alignment == 0u ? 1u : alignment;
            const uint64_t aligned = ((static_cast<uint64_t>(nextGuestAddress) + effectiveAlignment - 1u) /
                                      effectiveAlignment) *
                                     effectiveAlignment;
            if (aligned + size > memory.size())
            {
                return 0u;
            }
            nextGuestAddress = static_cast<uint32_t>(aligned + size);
            guestAllocations.push_back(static_cast<uint32_t>(aligned));
            return static_cast<uint32_t>(aligned);
        }

        void freeGuest(uint32_t address) override
        {
            freedGuestAddresses.push_back(address);
        }

        uint32_t nextIopAddress=0x40000;
        std::vector<uint32_t> iopAllocations,freedIopAddresses;
        uint32_t allocateIopMemory(uint32_t size) override
        {
            if (!size || size>memory.size() || nextIopAddress>memory.size()-size) return 0;
            const auto p=nextIopAddress; nextIopAddress=(p+size+63)&~63u;
            iopAllocations.push_back(p); return p;
        }
        void freeIopMemory(uint32_t address) override {freedIopAddresses.push_back(address);}

        void audioCommand(uint32_t sid,
                          uint32_t function,
                          GuestBuffer send,
                          GuestBuffer receive) override
        {
            lastAudioSid = sid;
            lastAudioFunction = function;
            lastAudioSend = send;
            lastAudioReceive = receive;
            ++audioCalls;
        }

        std::string hostPath(HostPathKind kind) const override
        {
            switch (kind)
            {
            case HostPathKind::CdRoot:
                return "fake/cd";
            case HostPathKind::CdImage:
                return "fake/disc.iso";
            case HostPathKind::HostRoot:
                return "fake/host";
            case HostPathKind::MemoryCardRoot:
                return "fake/mc0";
            default:
                return "fake/elf";
            }
        }

        std::string translateGuestPath(std::string_view path) const override
        {
            return "translated/" + std::string(path);
        }

        uint64_t openHostFile(std::string_view path) override
        {
            const auto file = hostFileContents.find(std::string(path));
            if (file == hostFileContents.end())
            {
                return 0u;
            }
            const uint64_t handle = nextHostFileHandle++;
            openHostFiles.emplace(handle, file->first);
            return handle;
        }

        bool hostFileSize(uint64_t handle, uint64_t &size) const override
        {
            size = 0u;
            const auto open = openHostFiles.find(handle);
            if (open == openHostFiles.end())
            {
                return false;
            }
            const auto file = hostFileContents.find(open->second);
            if (file == hostFileContents.end())
            {
                return false;
            }
            size = file->second.size();
            return true;
        }

        bool readHostFile(uint64_t handle,
                          uint64_t offset,
                          void *destination,
                          size_t size,
                          size_t &bytesRead) override
        {
            bytesRead = 0u;
            if (!destination && size != 0u)
            {
                return false;
            }
            const auto open = openHostFiles.find(handle);
            if (open == openHostFiles.end())
            {
                return false;
            }
            const auto file = hostFileContents.find(open->second);
            if (file == hostFileContents.end() || offset > file->second.size())
            {
                return false;
            }
            bytesRead = std::min<size_t>(size, file->second.size() - static_cast<size_t>(offset));
            if (bytesRead != 0u)
            {
                std::memcpy(destination,
                            file->second.data() + static_cast<size_t>(offset),
                            bytesRead);
            }
            return true;
        }

        void closeHostFile(uint64_t handle) override
        {
            if (openHostFiles.erase(handle) != 0u)
            {
                closedHostFileHandles.push_back(handle);
            }
        }

        int32_t memoryCard(const MemoryCardRequest &request) override
        {
            lastMemoryCardRequest = request;
            ++memoryCardCalls;
            return 0;
        }

        bool hasGuestFunction(uint32_t address) const override
        {
            return address == guestFunctionAddress;
        }

        bool invokeGuestFunction(uint64_t callToken,
                                 uint32_t address,
                                 uint32_t a0,
                                 uint32_t a1,
                                 uint32_t a2,
                                 uint32_t a3,
                                 uint32_t *resultAddress) override
        {
            if (!hasGuestFunction(address))
            {
                return false;
            }
            lastCallToken = callToken;
            lastGuestArguments = {a0, a1, a2, a3};
            if (resultAddress)
            {
                *resultAddress = guestFunctionResult;
            }
            return true;
        }

        void log(LogLevel level, std::string_view message) override
        {
            logs.emplace_back(level, std::string(message));
        }

        bool writeWord(uint32_t address, uint32_t value)
        {
            return writeGuest(address, &value, sizeof(value));
        }

        uint32_t readWord(uint32_t address) const
        {
            uint32_t value = 0u;
            (void)readGuest(address, &value, sizeof(value));
            return value;
        }

        bool hasLog(std::string_view expected) const
        {
            return std::any_of(logs.begin(), logs.end(), [&](const auto &entry)
                               { return entry.second == expected; });
        }

        std::vector<uint8_t> memory;
        uint32_t nextHandle = 0x8000u;
        uint32_t nextGuestAddress = 0x4000u;
        std::vector<uint32_t> guestAllocations;
        std::vector<uint32_t> freedGuestAddresses;
        uint32_t audioCalls = 0u;
        uint32_t lastAudioSid = 0u;
        uint32_t lastAudioFunction = 0u;
        GuestBuffer lastAudioSend{};
        GuestBuffer lastAudioReceive{};
        uint32_t memoryCardCalls = 0u;
        MemoryCardRequest lastMemoryCardRequest{};
        uint32_t guestFunctionAddress = 0x2000u;
        uint32_t guestFunctionResult = 0x3000u;
        uint64_t lastCallToken = 0u;
        std::vector<uint32_t> lastGuestArguments;
        std::vector<std::pair<LogLevel, std::string>> logs;
        std::unordered_map<std::string, std::vector<uint8_t>> hostFileContents;
        std::unordered_map<uint64_t, std::string> openHostFiles;
        std::vector<uint64_t> closedHostFileHandles;
        uint64_t nextHostFileHandle = 1u;

    private:
        bool contains(uint32_t address, size_t size) const
        {
            const uint64_t end = static_cast<uint64_t>(address) + static_cast<uint64_t>(size);
            return end <= memory.size();
        }
    };

    bool containsDiagnostic(const DebugSnapshot &snapshot, std::string_view text)
    {
        return std::any_of(snapshot.diagnostics.begin(), snapshot.diagnostics.end(), [&](const std::string &diagnostic)
                           { return diagnostic.find(text) != std::string::npos; });
    }

    const DebugService *findService(const DebugSnapshot &snapshot, std::string_view name)
    {
        const auto it = std::find_if(snapshot.services.begin(), snapshot.services.end(), [&](const DebugService &service)
                                     { return service.name == name; });
        return it == snapshot.services.end() ? nullptr : &*it;
    }

    uint64_t metricValue(const DebugService &service, std::string_view name)
    {
        const auto it = std::find_if(service.metrics.begin(), service.metrics.end(), [&](const DebugMetric &metric)
                                     { return metric.name == name; });
        return it == service.metrics.end() ? std::numeric_limits<uint64_t>::max() : it->value;
    }

    bool pluginModuleIsLoaded(const std::filesystem::path &path)
    {
#if defined(_WIN32)
        return GetModuleHandleW(path.c_str()) != nullptr;
#elif defined(__linux__)
        void *handle = dlopen(path.c_str(), RTLD_NOW | RTLD_NOLOAD);
        if (!handle)
        {
            return false;
        }
        dlclose(handle);
        return true;
#else
        (void)path;
        return false;
#endif
    }
}

void register_ps2_iop_tests()
{
    MiniTest::Case("PS2IopSubsystem", [](TestCase &tc)
    {
        tc.Run("Shaolin Monks transport requires exact identity and preserves unknown URPC", [](TestCase &t)
        {
            FakeIopHost host;
            ps2x::iop::IopSubsystem subsystem(host);
            std::string error;
            t.IsTrue(subsystem.configure({"SLUS_210.87", 0x11C070u, 0x1A37A67Cu}, &error), "exact game configures");
            t.Equals(subsystem.debugSnapshot().activeProfile, std::string("mk-shaolin-monks-us"), "select MK profile");
            const std::array<uint32_t,4> packet{0u, 0x2000u, 0x3000u, 0x80u};
            host.writeGuest(0x1000u, packet.data(), sizeof(packet));
            ps2x::iop::RpcRequest request{};
            request.sid = 0x90000200u;
            request.function = 2u;
            request.send = {0x1000u, sizeof(packet)};
            request.receive = {0x1100u, 4u};
            t.IsTrue(subsystem.handleRpc(request).handled, "create transport is implemented");
            const uint32_t handle = host.readWord(0x1100u);
            t.IsTrue(handle != 0u, "create returns an allocated remote handle");
            t.IsTrue(subsystem.handleRpc(request).handled, "repeated create is handled");
            t.Equals(host.readWord(0x1100u), handle, "same transport ID preserves identity");
            request.function = 0x47Fu;
            t.IsFalse(subsystem.handleRpc(request).handled, "unmapped URPC remains unhandled");
            t.IsTrue(subsystem.configure({"SLUS_210.87", 0x11C070u, 0x1A37A67Du}, &error), "other build configures core only");
            t.IsTrue(subsystem.debugSnapshot().activeProfile.empty(), "wrong CRC cannot select MK profile");
            request.function = 2u;
            t.IsFalse(subsystem.handleRpc(request).handled, "other build does not receive MK transport");
            t.IsTrue(subsystem.configure({"SLUS_210.87", 0x11C074u, 0x1A37A67Cu}, &error), "other entry configures core only");
            t.IsTrue(subsystem.debugSnapshot().activeProfile.empty(), "wrong entry cannot select MK profile");
        });
        tc.Run("MK CRI distinct objects and descriptor ownership across DTX", [](TestCase &t)
        {
            FakeIopHost host(0x20000);
            IopSubsystem subsystem(host);std::string error;
            t.IsTrue(subsystem.configure({"SLUS_210.87",0x11c070,0x1a37a67c},&error),"configure MK");
            auto rpc=[&](uint32_t command,std::initializer_list<uint32_t> words,uint32_t receive=4u) {
                std::vector<uint32_t> packet(words);
                host.writeGuest(0x1000,packet.data(),packet.size()*4);
                RpcRequest r{};r.sid=0x90000200;r.function=command;r.send={0x1000,static_cast<uint32_t>(packet.size()*4)};r.receive={0x1100,receive};
                const auto result=subsystem.handleRpc(r);
                t.IsTrue(result.handled,"verified CRI command handled");return host.readWord(0x1100);
            };
            const auto transportHandle=rpc(2,{0,0x2000,0x3000,0x100});
            const auto left=rpc(0x422,{1,0x4000,0x40});
            const auto right=rpc(0x422,{1,0x4040,0x40});
            t.IsTrue(left!=right && left!=1 && right!=1,"distinct queue handles");
            const auto bridge=rpc(0x400,{0x5000,left,1,0x5100});
            rpc(0x402,{bridge,0x1234});
            const auto voice=rpc(0x408,{2,0,left,right});
            const auto voice2=rpc(0x408,{2,0,left,right});
            t.IsTrue(voice!=voice2 && voice!=2,"distinct RNA objects, not channel count");
            // Replay the actual startup RNA packet: flag, rate, signed L/R pan.
            rpc(2,{1,0x2200,0x3200,0x100});
            std::array<uint32_t,20> controls{4,0,0,0,
                8,voice,0,0, 4,voice,48000,0,
                9,voice,0,0xfffffff1u, 9,voice,1,15};
            host.writeGuest(0x2200,controls.data(),sizeof(controls));
            auto audioDma=[&] {subsystem.onSifTransfer({SifTransferKind::SetDma,SifTransferPhase::AfterCopy,0x2200,0x3200,0x100});};
            audioDma();
            t.Equals(host.readWord(0x2200),0u,"RNA controls produce empty completion");
            t.Equals(host.readWord(0x22fc),1u,"RNA control batch acknowledged");
            controls[0]=1;controls[4]=9;controls[6]=2;
            host.writeGuest(0x2200,controls.data(),sizeof(controls));audioDma();
            t.Equals(host.readWord(0x22fc),1u,"invalid pan channel cannot corrupt another object");
            host.writeWord(0x2000,1);host.writeWord(0x2004,0x12345678);host.writeWord(0x2010,0x12340100);
            host.writeWord(0x2014,bridge);host.writeWord(0x2018,0x6000);host.writeWord(0x201c,128);
            auto dma=[&] {subsystem.onSifTransfer({SifTransferKind::SetDma,SifTransferPhase::AfterCopy,0x2000,0x3000,0x100});};
            dma();
            t.Equals(host.readWord(0x2000),0u,"no completed playback echoed");
            t.Equals(host.readWord(0x2004),0x12345678u,"reserved DTX header preserved");
            t.Equals(host.readWord(0x20fc),1u,"transport acknowledged after queueing");
            t.Equals(rpc(0x429,{left,1}),128u,"all PCM remains owned by data queue");
            t.Equals(rpc(0x429,{left,0}),0u,"no room before consumption");
            t.Equals(rpc(0x426,{left,1,48},8),0x6000u,"partial chunk address");
            t.Equals(host.readWord(0x1104),48u,"partial chunk length");
            rpc(0x428,{left,0,0x6000,48});
            host.writeWord(0x2000,0);dma();
            t.Equals(host.readWord(0x2000),1u,"one returned consumed chunk");
            t.Equals(host.readWord(0x2010),0x12340000u,"return line and xid");
            t.Equals(host.readWord(0x2014),0x5100u,"return routed to EE bridge");
            t.Equals(host.readWord(0x2018),0x6000u,"return original chunk pointer");
            t.Equals(host.readWord(0x201c),48u,"only consumed length returned");
            t.Equals(rpc(0x429,{left,1}),80u,"unconsumed tail remains queued");
            t.Equals(rpc(0x429,{left,0}),0u,"returned room removed from IOP queue");
            host.writeWord(0x2000,0);dma();
            t.Equals(host.readWord(0x2000),0u,"no double return");
            const auto before=host.readWord(0x20fc);
            host.writeWord(0x2000,2);host.writeWord(0x2010,0);host.writeWord(0x2014,bridge);
            host.writeWord(0x2018,0x7000);host.writeWord(0x201c,8);
            host.writeWord(0x2020,0);host.writeWord(0x2024,0xdeadbeef);
            host.writeWord(0x2028,0x8000);host.writeWord(0x202c,8);dma();
            t.Equals(host.readWord(0x20fc),before,"invalid batch not acknowledged");
            t.Equals(rpc(0x429,{left,1}),80u,"invalid batch has no partial enqueue");
            // A failed reply cannot remove a queued descriptor.
            std::array<uint32_t,3> get{left,1,80};host.writeGuest(0x1000,get.data(),sizeof(get));
            RpcRequest r{};r.sid=0x90000200;r.function=0x426;r.send={0x1000,sizeof(get)};r.receive={0x1fffe,8};
            t.IsFalse(subsystem.handleRpc(r).handled,"reject unwritable result");
            t.Equals(rpc(0x429,{left,1}),80u,"failed reply preserves ownership");
            const auto lastTicket=host.readWord(0x20fc);
            rpc(3,{transportHandle});host.writeWord(0x2000,0);dma();
            t.Equals(host.readWord(0x20fc),lastTicket,"destroyed transport cannot acknowledge DMA");
        });
        tc.Run("MK PCM returns guest buffers only after host consumption", [](TestCase &t)
        {
            FakeIopHost host(0x20000);IopSubsystem subsystem(host);std::string error;
            subsystem.configure({"SLUS_210.87",0x11c070,0x1a37a67c},&error);
            auto rpc=[&](uint32_t command,std::initializer_list<uint32_t> words) {
                std::vector<uint32_t> packet(words);host.writeGuest(0x1000,packet.data(),packet.size()*4);
                RpcRequest r{};r.sid=0x90000200;r.function=command;r.send={0x1000,static_cast<uint32_t>(packet.size()*4)};r.receive={0x1100,4};
                t.IsTrue(subsystem.handleRpc(r).handled,"PCM setup RPC");return host.readWord(0x1100);
            };
            rpc(2,{1,0x2000,0x3000,0x100});
            const auto left=rpc(0x422,{1,0x4000,0x100}),right=rpc(0x422,{1,0x4100,0x100});
            const auto voice=rpc(0x408,{2,0,left,right});
            std::array<int16_t,32> l{1000,-1000,32767,-32768,1,-1,123,-456},r{-700,900,-222,333,4,-5,6,-7};
            host.writeGuest(0x5000,l.data(),sizeof(l));host.writeGuest(0x6000,r.data(),sizeof(r));
            rpc(0x428,{left,1,0x5000,sizeof(l)});rpc(0x428,{right,1,0x6000,sizeof(r)});
            std::array<uint32_t,16> control{3,0,0,0,9,voice,0,0xfffffff1u,9,voice,1,15,0,voice,0,0};
            host.writeGuest(0x2000,control.data(),sizeof(control));
            auto dma=[&] {subsystem.onSifTransfer({SifTransferKind::SetDma,SifTransferPhase::AfterCopy,0x2000,0x3000,0x100});};
            dma();t.Equals(rpc(0x429,{left,1}),64u,"unavailable device retains data");
            host.enablePcm=true;host.acceptPcm=false;host.writeWord(0x2000,0);dma();
            t.Equals(rpc(0x429,{left,1}),64u,"host backpressure retains both channels");
            host.acceptPcm=true;dma();
            t.Equals(rpc(0x429,{left,1}),0u,"submitted source moved to in-flight ownership");
            t.Equals(rpc(0x429,{left,0}),0u,"submission alone returns no room");
            const auto& samples=host.pcm.at(voice).samples;
            t.Equals(samples.size(),size_t(64),"stereo frame count");
            for(size_t i=0;i<32;++i) {
                t.Equals(samples[i*2],l[i],"signed left PCM and full-left pan");
                t.Equals(samples[i*2+1],r[i],"signed right PCM and full-right pan");
            }
            host.pcm.at(voice).consumed=31;dma();
            t.Equals(rpc(0x429,{left,0}),0u,"partial device pull cannot return unfinished chunk");
            host.pcm.at(voice).consumed=32;dma();
            t.Equals(rpc(0x429,{left,0}),64u,"consumed left chunk returned");
            t.Equals(rpc(0x429,{right,0}),64u,"consumed right chunk returned");
            dma();t.Equals(rpc(0x429,{left,0}),64u,"no duplicate return on next poll");
            rpc(0x428,{left,1,0x5040,16});rpc(0x428,{right,1,0x6040,16});dma();
            t.Equals(rpc(0x429,{left,1}),16u,"sub-32-sample tail retained for converter minimum");
            rpc(0x409,{voice});t.IsTrue(host.pcm.empty(),"destroy closes host stream");
        });
        tc.Run("MK RNA destruction returns in-flight buffers for movie restart", [](TestCase &t)
        {
            FakeIopHost host(0x20000);host.enablePcm=true;
            IopSubsystem subsystem(host);std::string error;
            subsystem.configure({"SLUS_210.87",0x11c070,0x1a37a67c},&error);
            auto rpc=[&](uint32_t command,std::initializer_list<uint32_t> words) {
                host.writeGuest(0x1000,words.begin(),words.size()*4);
                RpcRequest r{};r.sid=0x90000200;r.function=command;
                r.send={0x1000,static_cast<uint32_t>(words.size()*4)};r.receive={0x1100,4};
                t.IsTrue(subsystem.handleRpc(r).handled,"restart RPC");return host.readWord(0x1100);
            };
            rpc(2,{1,0x2000,0x3000,0x100});
            const auto left=rpc(0x422,{1,0x4000,0x100}),right=rpc(0x422,{1,0x4100,0x100});
            auto voice=rpc(0x408,{2,0,left,right});
            rpc(0x428,{left,1,0x5000,0x4000});rpc(0x428,{right,1,0xa000,0x4000});
            auto dma=[&] {subsystem.onSifTransfer({SifTransferKind::SetDma,SifTransferPhase::AfterCopy,0x2000,0x3000,0x100});};
            auto play=[&] {
                const uint32_t control[]={1,0,0,0,0,voice,0,0};
                host.writeGuest(0x2000,control,sizeof(control));dma();host.writeWord(0x2000,0);
            };
            play();host.pcm.at(voice).consumed=100;
            t.Equals(rpc(0x429,{left,0}),0u,"no completed chunk before cancellation");
            t.Equals(rpc(0x429,{left,1}),0x3000u,"remaining data excludes eight in-flight chunks");
            host.writeWord(0x1000,voice);
            RpcRequest bad{};bad.sid=0x90000200;bad.function=0x409;
            bad.send={0x1000,4};bad.receive={0x20000,4};
            t.IsTrue(!subsystem.handleRpc(bad).handled,"invalid destroy reply rejected");
            t.IsTrue(host.pcm.contains(voice),"failed destroy preserves device ownership");
            t.Equals(rpc(0x429,{left,0}),0u,"failed destroy returns nothing");
            rpc(0x409,{voice});
            t.IsTrue(host.pcm.empty(),"cancelled sink closed before reuse");
            for(auto q:{left,right}) {
                t.Equals(rpc(0x429,{q,0}),0x1000u,"cancelled in-flight bytes returned once");
                t.Equals(rpc(0x429,{q,1}),0x3000u,"queued source data retained");
            }
            dma();t.Equals(rpc(0x429,{left,0}),0x1000u,"destroy cannot duplicate returns");
            voice=rpc(0x408,{2,0,left,right});play();
            // New object drains the old converter input; the retail worker waits
            // for all 16 KiB before enabling the next movie's EE transfers.
            for(unsigned i=0;i<4;++i) {
                host.pcm.at(voice).consumed=host.pcm.at(voice).samples.size()/2;dma();
            }
            for(auto q:{left,right}) {
                t.Equals(rpc(0x429,{q,0}),0x4000u,"full room restored for original restart gate");
                t.Equals(rpc(0x429,{q,1}),0u,"old source drained");
            }
            rpc(0x409,{voice});
            t.Equals(rpc(0x429,{left,0}),0x4000u,"fully consumed teardown adds no duplicate room");
        });
        tc.Run("MK SNDF initialization and original IOP bank layout fixtures", [](TestCase &t)
        {
            struct Case {int32_t result;uint32_t ready;std::array<uint32_t,48> h,w,hbase,wbase;};
            const Case cases[]={
#include "../../games/mk-shaolin-monks/tests/sndf_layout_cases.inc"
            };
            for(const auto& c:cases)
            {
                FakeIopHost host(0x80000);
                IopSubsystem subsystem(host);
                std::string error;
                subsystem.configure({"SLUS_210.87",0x11c070,0x1a37a67c},&error);
                auto metric=[&](std::string_view name)->uint64_t {
                    for(const auto& svc:subsystem.debugSnapshot().services)
                        for(const auto& m:svc.metrics)if(m.name==name)return m.value;
                    return UINT64_MAX;
                };
                RpcRequest r{}; r.sid=0x534e4446; r.send.address=r.receive.address=0x1000;
                auto rpc=[&](uint32_t fn,std::initializer_list<uint32_t> words,uint32_t reply=4) {
                    r.function=fn;r.send.size=uint32_t(words.size()*4);r.receive.size=reply;
                    host.writeGuest(0x1000,words.begin(),r.send.size);
                    t.IsTrue(subsystem.handleRpc(r).handled,"SNDF control handled");
                    return static_cast<int32_t>(host.readWord(0x1000));
                };
                t.Equals(rpc(0x1300,{0,0,0,1,0,0}),-1,"original zero work size error");
                t.Equals(rpc(0x1300,{0x30000,0,0,1,0,0}),0,"setup");
                t.Equals(rpc(0x100,{48000,0x6c3},8),0,"initialize");
                t.Equals(host.readWord(0x1004),0x40000u,"IOP work pointer returned");
                t.IsTrue(host.guestAllocations.empty(),"does not allocate from EE heap");
                t.Equals(host.iopAllocations.size(),size_t(2),"work and command queue allocated");
                host.memory[0x2fff]=0xa5;host.memory[0x5b00]=0x5a;
                t.Equals(rpc(0x1600,{0x3000}),int32_t(0x70000),"backed command queue pointer returned");
                uint64_t hash=14695981039346656037ull;
                for(size_t i=0x3000;i<0x5b00;++i)hash=(hash^host.memory[i])*1099511628211ull;
                constexpr uint64_t expectedHash=
#include "../../games/mk-shaolin-monks/tests/sndf_reset_hash.inc"
                ;
                t.Equals(hash,expectedHash,"complete status matches original CPU reset fingerprint");
                t.Equals(host.memory[0x2fff],uint8_t(0xa5),"left status guard");
                t.Equals(host.memory[0x5b00],uint8_t(0x5a),"right status guard");
                r.function=0x300;r.send.size=384;r.receive.size=4;
                host.writeGuest(0x1000,c.h.data(),192);host.writeGuest(0x10c0,c.w.data(),192);
                t.IsTrue(subsystem.handleRpc(r).handled,"partition RPC");
                t.Equals(static_cast<int32_t>(host.readWord(0x1000)),c.result,"original partition result");
                t.Equals(metric("sndf_layout_ready"),uint64_t(c.ready),"original partition ready flag");
                t.Equals(metric("sndf_header_base_0"),uint64_t(c.hbase[0]),"first original IOP partition");
                t.Equals(metric("sndf_header_base_1"),uint64_t(c.hbase[1]),"second original IOP partition");
                t.Equals(metric("sndf_wave_base_0"),uint64_t(c.wbase[0]),"first original SPU partition");
                t.Equals(metric("sndf_wave_base_1"),uint64_t(c.wbase[1]),"second original SPU partition");
                r.function=0xfe00;r.send.size=0;r.receive.size=384;
                t.IsTrue(subsystem.handleRpc(r).handled,"partition query");
                for(unsigned i=0;i<48;++i)
                {
                    t.Equals(host.readWord(0x1000+i*4),c.ready?c.h[i]:0u,"header capacity");
                    t.Equals(host.readWord(0x10c0+i*4),c.ready?c.w[i]:0u,"wave capacity");
                }
                r.function=0xa00;r.receive.size=4;
                t.IsTrue(subsystem.handleRpc(r).handled,"registration request handled");
                t.Equals(static_cast<int32_t>(host.readWord(0x1000)),c.ready?-8:-4,"reject absent bank data or layout");
                subsystem.reset();
                t.Equals(host.freedIopAddresses.size(),size_t(2),"reset releases owned IOP storage");
            }
            FakeIopHost host(0x50000);IopSubsystem subsystem(host);std::string error;
            subsystem.configure({"SLUS_210.87",0x11c070,0x1a37a67c},&error);
            RpcRequest r{};r.sid=0x534e4446;r.function=0x1300;r.send={0x1000,24};r.receive={0x1000,4};
            const uint32_t args[]={0x30000,0,0,1,0,0};host.writeGuest(0x1000,args,24);
            t.IsTrue(subsystem.handleRpc(r).handled,"failure setup");r.function=0x100;r.send.size=r.receive.size=8;
            t.IsTrue(subsystem.handleRpc(r).handled,"allocation failure is a handled error");
            t.Equals(static_cast<int32_t>(host.readWord(0x1000)),-2,"original allocation error");
            t.IsTrue(host.iopAllocations.empty(),"no allocations leaked on failure");
        });
        tc.Run("MK SNDF rejects malformed ranges and releases only owned IOP allocations", [](TestCase &t)
        {
            FakeIopHost host(0x80000);IopSubsystem subsystem(host);std::string error;
            subsystem.configure({"SLUS_210.87",0x11c070,0x1a37a67c},&error);
            RpcRequest r{};r.sid=0x534e4446;r.function=0x1300;r.send={0x1000,24};r.receive={0x1000,4};
            uint32_t args[]={0x30000,0x10000,0,1,0,0};host.writeGuest(0x1000,args,24);
            r.send.size=23;t.IsFalse(subsystem.handleRpc(r).handled,"truncated configuration");
            r.send.size=24;r.receive.address=0xfffffffe;
            t.IsFalse(subsystem.handleRpc(r).handled,"wrapping reply");
            r.receive.address=0x1000;t.IsTrue(subsystem.handleRpc(r).handled,"external IOP work buffer");
            r.function=0x100;r.send.size=r.receive.size=8;args[0]=48000;args[1]=0;
            host.writeGuest(0x1000,args,8);t.IsTrue(subsystem.handleRpc(r).handled,"external init");
            t.Equals(host.readWord(0x1004),0x10000u,"caller-owned work pointer preserved");
            t.Equals(host.iopAllocations.size(),size_t(1),"only command queue allocated");
            r.function=0x1600;r.send.size=r.receive.size=4;args[0]=0x7f000;host.writeGuest(0x1000,args,4);
            t.IsFalse(subsystem.handleRpc(r).handled,"invalid status publication range");
            r.function=0x300;r.send.size=384;host.zeroGuest(0x1000,384);
            const uint32_t overflow=0x30001;host.writeGuest(0x1000+47*4,&overflow,4);
            t.IsTrue(subsystem.handleRpc(r).handled,"final-partition bounds checked");
            t.Equals(static_cast<int32_t>(host.readWord(0x1000)),-1,"reject last-partition overflow for host safety");
            subsystem.reset();t.Equals(host.freedIopAddresses.size(),size_t(1),"external work not freed");
            t.Equals(host.freedIopAddresses[0],0x40000u,"owned queue freed");
        });
        tc.Run("MK SNDF bank validation errors and atomic tone-program bounds", [](TestCase &t)
        {
            FakeIopHost host(0x80000);IopSubsystem subsystem(host);std::string error;
            subsystem.configure({"SLUS_210.87",0x11c070,0x1a37a67c},&error);
            RpcRequest r{};r.sid=0x534e4446;r.send.address=r.receive.address=0x1000;
            auto rpc=[&](uint32_t fn,std::initializer_list<uint32_t> args,uint32_t output=4){
                r.function=fn;r.send.size=uint32_t(args.size()*4);r.receive.size=output;
                host.writeGuest(0x1000,args.begin(),r.send.size);
                t.IsTrue(subsystem.handleRpc(r).handled,"bank validation RPC handled");
                return static_cast<int32_t>(host.readWord(0x1000));
            };
            t.Equals(rpc(0xa00,{}),-4,"no layout");
            rpc(0x1300,{0x30000,0,0,1,0,0});rpc(0x100,{48000,0},8);rpc(0x1600,{0x3000});
            host.zeroGuest(0x1000,384);const uint32_t cap=0x1000;
            for(unsigned i=0;i<3;++i){host.writeGuest(0x1000+i*4,&cap,4);host.writeGuest(0x10c0+i*4,&cap,4);}
            r.function=0x300;r.send.size=384;r.receive.size=4;t.IsTrue(subsystem.handleRpc(r).handled,"layout");
            t.Equals(rpc(0xa00,{}),-8,"invalid magic");
            for(unsigned slot=0;slot<3;++slot)
            {
                const auto b=0x40000+slot*0x1000;
                const uint32_t magic[]={0x5f327370,0x4b505444};host.writeGuest(b,magic,8);
                host.memory[b+8]=1;host.memory[b+9]=0x47;
                t.Equals(rpc(0xa00+slot,{}),-7,"old version");
                host.memory[b+8]=2;host.memory[b+0x89]=slot<2?255:3;
                const uint32_t id=slot,hs=0x100,ws=0x200;
                host.writeGuest(b+0x84,&id,4);host.writeGuest(b+0x10,&hs,4);host.writeGuest(b+0x18,&ws,4);
                if(slot==0)
                {
                    const auto before=std::vector<uint8_t>(host.memory.begin()+0x3000,host.memory.begin()+0x5b00);
                    const uint32_t bad=0xfffffff0;host.writeGuest(b+0x98,&bad,4);host.memory[b+0xc2]=0x40;
                    t.Equals(rpc(0xa00,{}),-8,"wrapping tone program rejected");
                    t.IsTrue(std::equal(before.begin(),before.end(),host.memory.begin()+0x3000),"failed program leaves published status unchanged");
                    host.memory[b+0xc2]=0;
                }
                t.Equals(rpc(0xa00+slot,{}),slot<2?0:-5,"original wave-count limit");
            }
        });
#ifdef MKSM_RETAIL_FIXTURES
        tc.Run("MK SPU voice original parameters, loop and release", [](TestCase &t)
        {
            using namespace ps2x::iop::detail;
            SndfVoice voice;voice.baseVolume=255;voice.angle=45;voice.distance=205;
            voice.volume(127,1);
            t.Equals(voice.gainLeft,0xbb5,"left volume matches original hardware write");
            t.Equals(voice.gainRight,0xbb5,"right volume matches original hardware write");
            t.Equals(SndfVoice::calculatePitch(-1347,0),uint16_t(0x759),"pitch matches original 56f0");
            t.Equals(SndfVoice::calculatePitch(-1347,1731),uint16_t(0x2b4),"old pitch input reference");
            auto sample=std::make_shared<SndfSample>();sample->blocks.assign(16,0x11);
            sample->blocks[0]=0;sample->blocks[1]=7;sample->loop=true;
            voice.sample=sample;voice.envelope.adsr1=0x80ff;voice.envelope.adsr2=0x5fc0;
            t.IsTrue(voice.start(),"loop sample starts");
            for(unsigned i=0;i<2800;++i)voice.render();
            t.IsTrue(voice.playing,"loop/end flags retain voice across 100 source loops");
            t.IsTrue(voice.render()>0,"loop remains audible");
            voice.envelope.release();for(unsigned i=0;i<16;++i)voice.render();
            t.IsFalse(voice.playing,"original release envelope terminates looping voice");
            sample->loop=false;
            t.IsTrue(voice.start(),"nonloop sample starts");
            for(unsigned i=0;i<27;++i)voice.render();
            t.IsTrue(voice.playing,"last block sample retained until rendered");
            voice.render();t.IsFalse(voice.playing,"exact 28-sample one-shot ends");
        });
        tc.Run("MK SNDF retail banks match original registration and tone initialization", [](TestCase &t)
        {
            const auto path=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"games/mk-shaolin-monks/logs/sndf-bank-fixture.bin";
            std::ifstream in(path,std::ios::binary);
            if(!in){t.Fail("Run probe-sndf-init.py first to generate the bounded retail fixture");return;}
            auto read32=[&](){uint8_t b[4]{};in.read(reinterpret_cast<char*>(b),4);return uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);};
            if(read32()!=0x4b4e4253 || read32()!=3){t.Fail("invalid bank fixture");return;}
            struct Fixture {uint32_t base,h,w;uint64_t toneHash;std::vector<uint8_t> header,status;};
            std::array<Fixture,3> fixtures;
            for(auto& f:fixtures)
            {
                f.base=read32();f.h=read32();f.w=read32();const auto lo=read32();f.toneHash=uint64_t(read32())<<32|lo;
                if(f.h>0x30000){t.Fail("oversized bank fixture");return;}
                f.header.resize(f.h);f.status.resize(0x2b00);
                in.read(reinterpret_cast<char*>(f.header.data()),f.h);in.read(reinterpret_cast<char*>(f.status.data()),0x2b00);
            }
            struct Unload {uint32_t slot;int32_t result;uint32_t waves;uint64_t hash;std::array<uint8_t,0x2b00> status;};
            std::array<Unload,4> unloads;
            t.Equals(read32(),4u,"four original unload cases");
            for(auto& u:unloads){u.slot=read32();u.result=static_cast<int32_t>(read32());u.waves=read32();
                const auto lo=read32();u.hash=uint64_t(read32())<<32|lo;
                in.read(reinterpret_cast<char*>(u.status.data()),u.status.size());}
            if(!in){t.Fail("truncated bank fixture");return;}
            FakeIopHost host(0x80000);IopSubsystem subsystem(host);std::string error;
            subsystem.configure({"SLUS_210.87",0x11c070,0x1a37a67c},&error);
            auto metric=[&](std::string_view name)->uint64_t {
                for(const auto& svc:subsystem.debugSnapshot().services)for(const auto& m:svc.metrics)if(m.name==name)return m.value;
                return UINT64_MAX;
            };
            RpcRequest r{};r.sid=0x534e4446;r.send.address=r.receive.address=0x1000;
            auto rpc=[&](uint32_t fn,std::initializer_list<uint32_t> args,uint32_t output=4){
                r.function=fn;r.send.size=uint32_t(args.size()*4);r.receive.size=output;
                host.writeGuest(0x1000,args.begin(),r.send.size);
                t.IsTrue(subsystem.handleRpc(r).handled,"retail bank RPC handled");
                return static_cast<int32_t>(host.readWord(0x1000));
            };
            t.Equals(rpc(0x1300,{0x30000,0,0,1,0,0}),0,"work setup");
            t.Equals(rpc(0x100,{48000,0},8),0,"init");
            rpc(0x1600,{0x3000});
            auto layout=[&](){
                host.zeroGuest(0x1000,384);
                for(unsigned i=0;i<3;++i){host.writeGuest(0x1000+i*4,&fixtures[i].h,4);host.writeGuest(0x10c0+i*4,&fixtures[i].w,4);}
                r.function=0x300;r.send.size=384;r.receive.size=4;
            };
            layout();t.IsTrue(subsystem.handleRpc(r).handled,"layout");
            for(unsigned i=0;i<3;++i)
            {
                const auto& f=fixtures[i];host.writeGuest(f.base,f.header.data(),f.header.size());
                t.Equals(rpc(0xa00+i,{}),0,"real bank registration succeeds");
                t.IsTrue(std::memcmp(host.memory.data()+0x3000,f.status.data(),f.status.size())==0,"all status bytes match original instructions");
                t.Equals(metric("sndf_tone_fingerprint"),f.toneHash,"all 512 tone records match original fingerprint");
                t.Equals(metric("sndf_registered_banks"),uint64_t(i+1),"bank count");
                t.Equals(rpc(0xa00+i,{}),-3,"duplicate registration rejected");
            }
            // Real ADPCM sample, original instruction-derived first voice command.
            std::ifstream adpcm(path.parent_path()/"sndf-adpcm-fixture.bin",std::ios::binary);
            std::array<uint32_t,3> sampleHeader{};
            adpcm.read(reinterpret_cast<char*>(sampleHeader.data()),12);
            if(!adpcm || sampleHeader[0]!=0x50444153 || sampleHeader[1]>0x10000){t.Fail("invalid ADPCM fixture");return;}
            host.soundMemory.resize(0x200000);
            adpcm.read(reinterpret_cast<char*>(host.soundMemory.data()+0x649b0),sampleHeader[1]);
            if(!adpcm){t.Fail("truncated ADPCM fixture");return;}
            host.enablePcm=true;
            auto sfsv=[&](std::initializer_list<std::array<uint32_t,5>> records){
                host.zeroGuest(0x10000,0xa40);unsigned n=0;
                for(const auto& record:records)host.writeGuest(0x10000+20*n++,record.data(),20);
                host.writeWord(0x10a00,n);
                RpcRequest call{};call.sid=0x53465356;call.function=0x8000;
                call.send={0x10000,0xa40};call.receive={0x12000,0x1c0};
                t.IsTrue(subsystem.handleRpc(call).handled,"voice update handled");return host.readWord(0x12000);
            };
            const std::array<uint32_t,5> prepare{3,0xf00,0,0x5700ae,0};
            const std::array<uint32_t,5> pan{0xffff0003,0x1200,0,45,205};
            const std::array<uint32_t,5> mode{0xffff0003,0x1d00,0,2,0};
            const std::array<uint32_t,5> start{0xffff0003,0x1000,0,0,0};
            t.Equals(sfsv({prepare,pan,mode,start,{9,0,0,0,0}}),1u,"unsupported trailing command retains whole batch");
            t.IsTrue(host.pcm.empty(),"no PCM created by rejected batch");
            t.Equals(host.readWord(0x55cc),0x7f000000u,"no partial voice setup");
            t.Equals(sfsv({prepare,pan,mode,start}),0u,"real first sound batch accepted");
            t.Equals(host.readWord(0x55cc),0x7f000101u,"voice status matches original start");
            if(host.pcm.size()!=1){t.Fail("one PCM sink required");return;}
            auto& audio=host.pcm.begin()->second;
            t.Equals(audio.rate,48000u,"SPU native sample rate");
            t.Equals(audio.samples.size(),size_t(2048),"bounded 1024-frame queue");
            for(int i=0;i<20;++i)sfsv({});
            t.Equals(host.readWord(0x55cc),0x7f000101u,"RPC polling cannot fabricate completion");
            t.Equals(audio.samples.size(),size_t(2048),"unconsumed PCM not overwritten");
            audio.consumed=1024;host.acceptPcm=false;sfsv({});
            t.Equals(audio.samples.size(),size_t(2048),"backpressure cannot advance playback");
            host.acceptPcm=true;
            for(unsigned i=0;i<100 && host.readWord(0x55cc)==0x7f000101u;++i){
                audio.consumed=audio.samples.size()/2;sfsv({});
            }
            t.Equals(host.readWord(0x55cc),0x7f000000u,"natural sample completion releases logical voice");
            t.Equals(metric("sndf_voices_completed"),uint64_t(1),"one consumed completion");
            t.Equals(audio.samples.size()/2,size_t((uint64_t(sampleHeader[2])*4096+0x758)/0x759),"original pitch determines sample duration");
            t.IsTrue(std::any_of(audio.samples.begin(),audio.samples.end(),[](int16_t x){return x!=0;}),"decoded voice produces real PCM");
            t.Equals(sfsv({prepare,pan,mode,start}),0u,"completed voice reusable");
            t.Equals(sfsv({{0xffff0003,0x1100,0,0,0}}),0u,"key-off accepted");
            t.Equals(host.readWord(0x55cc),0x7f000201u,"key-off matches original stopping status");
            for(unsigned i=0;i<4;++i){audio.consumed=audio.samples.size()/2;sfsv({});}
            t.Equals(host.readWord(0x55cc),0x7f000000u,"release envelope drains before completion");
            t.Equals(sfsv({prepare,pan,mode,start}),0u,"released voice reusable");
            t.Equals(sfsv({{0xffff0003,0x1f00,0,0,0}}),0u,"explicit release accepted");
            t.Equals(host.readWord(0x55cc),0x7f000000u,"explicit release immediately clears logical ownership");
            layout();const uint32_t smaller=0x100;host.writeGuest(0x1000,&smaller,4);
            t.IsTrue(subsystem.handleRpc(r).handled,"active header repartition");
            t.Equals(static_cast<int32_t>(host.readWord(0x1000)),-2,"loaded header cannot be shrunk");
            layout();host.writeGuest(0x10c0,&smaller,4);
            t.IsTrue(subsystem.handleRpc(r).handled,"active wave repartition");
            t.Equals(static_cast<int32_t>(host.readWord(0x1000)),-6,"loaded wave partition cannot be shrunk");
            t.Equals(sfsv({{0x00200001,0,0,0xa0000100,0},{0xffff0003,0xf00,0xffffffff,0x200500a0,0}}),0u,"original teardown batch accepted");
            for(const auto& u:unloads){
                t.Equals(rpc(0x700+u.slot,{}),u.result,"original bank unload result");
                t.IsTrue(std::memcmp(host.memory.data()+0x3000,u.status.data(),u.status.size())==0,"unload status matches original instructions");
                t.Equals(metric("sndf_tone_fingerprint"),u.hash,"unload tone fingerprint matches original");
                t.Equals(metric("sndf_wave_count"),uint64_t(u.waves),"unload wave ownership matches original");
            }
            subsystem.reset();t.IsTrue(host.pcm.empty(),"reset closes owned PCM sink");
        });
#endif
        tc.Run("MK SFSV idle update, backpressure and packet boundaries", [](TestCase &t)
        {
            FakeIopHost host;
            IopSubsystem subsystem(host);
            std::string error;
            subsystem.configure({"SLUS_210.87", 0x11C070u, 0x1A37A67Cu}, &error);
            RpcRequest request{};
            request.sid = 0x53465356u;
            request.function = 0x8000u;
            request.mode = 1u;
            request.send = {0x1000u, 0xA40u};
            request.receive = {0x3000u, 0x1C0u};
            request.endFunction = 0x463880u;
            request.endParameter = request.receive.address;
            std::array<uint8_t, 0x1C8> sentinel;
            sentinel.fill(0xA5);
            host.writeGuest(0x2FFCu, sentinel.data(), sentinel.size());
            // Stale records must not count as commands when the count at +A00 is zero.
            const uint32_t stale = 0x12345678u;
            host.writeGuest(0x1000u, &stale, 4);
            auto result = subsystem.handleRpc(request);
            t.IsTrue(result.handled, "idle poll handled");
            t.Equals(result.resultAddress, 0x3000u, "return receive packet");
            t.IsTrue(result.signalNowaitCompletion, "NOWAIT completes");
            t.Equals(result.callbackPolicy, CallbackPolicy::RuntimeDefault, "preserve guest callback");
            std::array<uint8_t, 0x1C0> reply;
            host.readGuest(0x3000u, reply.data(), reply.size());
            t.IsTrue(std::all_of(reply.begin(), reply.end(), [](uint8_t b){return b == 0;}), "idle response has no fabricated notifications");
            t.Equals(host.readWord(0x2FFCu), 0xA5A5A5A5u, "left guard preserved");
            t.Equals(host.readWord(0x31C0u), 0xA5A5A5A5u, "right guard preserved");
            t.Equals(host.readWord(0x1000u), stale, "input not modified");
            uint32_t count = 1;
            host.writeGuest(0x1A00u, &count, 4);
            result = subsystem.handleRpc(request);
            t.IsTrue(result.handled, "nonempty batch receives backpressure");
            t.Equals(host.readWord(0x3000u), 1u, "busy preserves EE batch");
            t.Equals(host.readWord(0x1A00u), 1u, "pending count retained");
            count = 128;
            host.writeGuest(0x1A00u, &count, 4);
            t.IsTrue(subsystem.handleRpc(request).handled, "full queue is valid");
            count = 129;
            host.writeGuest(0x1A00u, &count, 4);
            t.IsFalse(subsystem.handleRpc(request).handled, "oversized count rejected");
            count = 0;
            host.writeGuest(0x1A00u, &count, 4);
            request.send.size--;
            t.IsFalse(subsystem.handleRpc(request).handled, "truncated packet rejected");
            request.send.size++;
            request.receive.size--;
            t.IsFalse(subsystem.handleRpc(request).handled, "short reply rejected");
            request.receive.size++;
            request.receive.address = 0xFFFFFF00u;
            t.IsFalse(subsystem.handleRpc(request).handled, "wrapping address rejected");
            request.receive.address = 0xFFF0u;
            t.IsFalse(subsystem.handleRpc(request).handled, "failed guest write cannot complete");
            request.receive.address = 0x3000u;
            request.function++;
            t.IsFalse(subsystem.handleRpc(request).handled, "unknown RPC remains visible");
            request.function--;
            subsystem.configure({"SLUS_210.87", 0x11C070u, 0x1A37A67Du}, &error);
            t.IsFalse(subsystem.handleRpc(request).handled, "other game build not intercepted");
        });
        tc.Run("MK SFSV applies output mode controls atomically", [](TestCase &t)
        {
            FakeIopHost host;
            IopSubsystem subsystem(host);
            std::string error;
            const GameIdentity identity{"SLUS_210.87", 0x11C070u, 0x1A37A67Cu};
            subsystem.configure(identity, &error);
            auto metric = [&](std::string_view name) -> uint64_t {
                const auto snapshot = subsystem.debugSnapshot();
                for (const auto &service : snapshot.services)
                    for (const auto &value : service.metrics)
                        if (value.name == name) return value.value;
                return UINT64_MAX;
            };
            RpcRequest request{};
            request.sid = 0x53465356u;
            request.function = 0x8000u;
            request.send = {0x1000u, 0xA40u};
            request.receive = {0x3000u, 0x1C0u};
            const uint32_t one = 1, two = 2;
            host.writeGuest(0x1A00u, &one, 4);
            std::array<uint32_t, 5> record{1, 0, 0, 0xA0000900u, 0};
            host.writeGuest(0x1000u, record.data(), 20);
            t.Equals(metric("output_mode"), uint64_t(1), "original driver default");
            t.IsTrue(subsystem.handleRpc(request).handled, "mono control handled");
            t.Equals(host.readWord(0x3000u), 0u, "batch accepted");
            t.Equals(metric("output_mode"), uint64_t(0), "mode actually changed");
            record[3] = 0x000800A0u;
            host.writeGuest(0x1000u, record.data(), 20);
            subsystem.handleRpc(request);
            t.Equals(metric("output_mode"), uint64_t(1), "byte-reversed stereo control normalized");
            record[3] = 0xA0000900u;
            host.writeGuest(0x1000u, record.data(), 20);
            record[3] = 0xA0002900u; // Surround output remains unsupported.
            host.writeGuest(0x1014u, record.data(), 20);
            host.writeGuest(0x1A00u, &two, 4);
            subsystem.handleRpc(request);
            t.Equals(host.readWord(0x3000u), 1u, "unsupported second record keeps batch busy");
            t.Equals(metric("output_mode"), uint64_t(1), "no partial commit");
            t.Equals(metric("output_mode_commands"), uint64_t(2), "no fabricated command execution");
            record[3] = 0xA0000800u;
            host.writeGuest(0x1014u, record.data(), 20);
            subsystem.handleRpc(request);
            t.Equals(host.readWord(0x3000u), 0u, "supported multi-command batch accepted");
            t.Equals(metric("output_mode"), uint64_t(1), "wire order respected");
            host.writeGuest(0x1A00u, &one, 4);
            request.receive.address = 0xFFF0;
            t.IsFalse(subsystem.handleRpc(request).handled, "invalid reply cannot commit state");
            t.Equals(metric("output_mode"), uint64_t(1), "failed write leaves mode unchanged");
            subsystem.configure(identity, &error);
            t.Equals(metric("output_mode"), uint64_t(1), "reset restores original default");
            t.Equals(metric("output_mode_commands"), uint64_t(0), "reset clears counters");
        });
        tc.Run("unknown SID remains unhandled without a matching profile", [](TestCase &t)
        {
            FakeIopHost host;
            ps2x::iop::IopSubsystem subsystem(host);

            std::string error;
            const bool configured = subsystem.configure({"unmatched.elf", 0x100000u, 0x12345678u}, &error);
            t.IsTrue(configured, "configuring an unmatched game should keep core-only IOP services available");

            ps2x::iop::RpcRequest request{};
            request.sid = 0xDEADC0DEu;
            request.function = 0x99u;
            const ps2x::iop::RpcResult result = subsystem.handleRpc(request);
            t.IsFalse(result.handled, "an unknown SID should not be claimed by the IOP subsystem");
            t.Equals(result.resultAddress, 0u, "an unknown SID should not return a guest result address");
            t.IsFalse(result.signalNowaitCompletion, "an unknown SID should not signal nowait completion");
            t.Equals(result.callbackPolicy, ps2x::iop::CallbackPolicy::RuntimeDefault,
                     "an unknown SID should preserve runtime callback handling");

            const ps2x::iop::DebugSnapshot snapshot = subsystem.debugSnapshot();
            t.IsTrue(snapshot.activeProfile.empty(), "an unmatched game should not activate a profile");
            t.IsTrue(snapshot.activeProvider.empty(), "an unmatched game should not report a profile provider");
        });

        tc.Run("built-in profiles select by ELF basename and keep core services active", [](TestCase &t)
        {
            FakeIopHost host;
            ps2x::iop::IopSubsystem subsystem(host);
            std::string error;

            t.IsTrue(subsystem.configure({"SLUS_201.84", 0u, 0u}, &error),
                     "RECVX profile should match case-insensitively by basename");
            ps2x::iop::DebugSnapshot snapshot = subsystem.debugSnapshot();
            t.Equals(snapshot.activeProfile, std::string("recvx-us"),
                     "RECVX ELF should select its built-in profile");
            t.IsNotNull(findService(snapshot, "TSNDDRV"),
                        "RECVX profile should register TSNDDRV");
            t.IsNotNull(findService(snapshot, "CRI DTX"),
                        "RECVX profile should register CRI DTX");
            t.IsNotNull(findService(snapshot, "dbcman"),
                        "core DBCMAN should remain active with a game profile");
            t.IsNotNull(findService(snapshot, "libsd"),
                        "core LIBSD should remain active with a game profile");
            t.IsNotNull(findService(snapshot, "MCSERV"),
                        "core MCSERV should remain active with a game profile");

            error.clear();
            t.IsTrue(subsystem.configure({"slus_203.88", 0u, 0u}, &error),
                     "Fatal Frame profile should configure after a different game");
            snapshot = subsystem.debugSnapshot();
            t.Equals(snapshot.activeProfile, std::string("fatal-frame-us"),
                     "reload should replace the active profile");
            t.IsNull(findService(snapshot, "CRI DTX"),
                     "reload should destroy services from the previous profile");
            t.IsNotNull(findService(snapshot, "SDRDRV"),
                        "Fatal Frame profile should expose SDRDRV");
        });

        tc.Run("two subsystem instances isolate profile state and reset deterministically", [](TestCase &t)
        {
            FakeIopHost hostA;
            FakeIopHost hostB;
            ps2x::iop::IopSubsystem subsystemA(hostA);
            ps2x::iop::IopSubsystem subsystemB(hostB);
            std::string error;
            t.IsTrue(subsystemA.configure({"SLUS_205.78", 0u, 0u}, &error),
                     "first LotR instance should configure");
            t.IsTrue(subsystemB.configure({"SLUS_205.78", 0u, 0u}, &error),
                     "second LotR instance should configure");

            ps2x::iop::RpcRequest request{};
            request.sid = 0x00012345u;
            request.receive = {0x1000u, 8u};

            t.IsTrue(subsystemA.handleRpc(request).handled,
                     "first instance should handle LotR sound RPC");
            t.Equals(hostA.readWord(0x1004u), 1u,
                     "first instance should start its counter at one");
            (void)subsystemA.handleRpc(request);
            t.Equals(hostA.readWord(0x1004u), 2u,
                     "first instance should advance independently");

            t.IsTrue(subsystemB.handleRpc(request).handled,
                     "second instance should handle LotR sound RPC");
            t.Equals(hostB.readWord(0x1004u), 1u,
                     "second instance must not inherit the first counter");

            subsystemA.reset();
            (void)subsystemA.handleRpc(request);
            t.Equals(hostA.readWord(0x1004u), 1u,
                     "reset should restore per-instance service state");
        });

        tc.Run("LotR sound update completes queued PlayStream slots", [](TestCase &t)
        {
            FakeIopHost host;
            ps2x::iop::IopSubsystem subsystem(host);
            std::string error;
            t.IsTrue(subsystem.configure({"SLUS_205.78", 0u, 0u}, &error),
                     "LotR profile should configure");

            constexpr uint32_t kSendAddress = 0x0800u;
            constexpr uint32_t kReceiveAddress = 0x1000u;
            constexpr uint16_t kStreamSlot = 7u;
            const std::array<uint16_t, 10> playStreamPacket = {
                1u, // command count
                1u, // PlayStream
                7u, // argument count
                0u,
                static_cast<uint16_t>(kStreamSlot << 8u),
                0u,
                0u,
                0u,
                0u,
                0u,
            };
            t.IsTrue(host.writeGuest(kSendAddress,
                                     playStreamPacket.data(),
                                     sizeof(playStreamPacket)),
                     "PlayStream command packet should fit in guest memory");

            ps2x::iop::RpcRequest request{};
            request.sid = 0x00012345u;
            request.send = {kSendAddress, sizeof(playStreamPacket)};
            request.receive = {kReceiveAddress, 0x100u};

            t.IsTrue(subsystem.handleRpc(request).handled,
                     "LotR sound service should handle PlayStream");
            t.Equals(host.readWord(kReceiveAddress), 1u,
                     "PlayStream response should expose one active record");
            const uint32_t packedStream = host.readWord(kReceiveAddress + 4u);
            t.Equals((packedStream >> 4u) & 0x3Fu,
                     static_cast<uint32_t>(kStreamSlot),
                     "active record should identify the queued EE stream slot");
            t.Equals(host.readWord(kReceiveAddress + 0x24u), 1u,
                     "response counter should follow the active record");

            const std::array<uint16_t, 5> statusPacket = {
                1u, // command count
                9u, // GetStatus
                2u, // argument count
                kStreamSlot,
                0u,
            };
            t.IsTrue(host.writeGuest(kSendAddress, statusPacket.data(), sizeof(statusPacket)),
                     "GetStatus command packet should fit in guest memory");
            request.send.size = sizeof(statusPacket);

            t.IsTrue(subsystem.handleRpc(request).handled,
                     "LotR sound service should handle the following status update");
            t.Equals(host.readWord(kReceiveAddress), 0u,
                     "the update after PlayStream should report no active records");
            t.Equals(host.readWord(kReceiveAddress + 4u), 2u,
                     "empty response counter should return to the base offset");
        });

        tc.Run("TSNDDRV uses profile checksum bindings without writing invalid ports", [](TestCase &t)
        {
            FakeIopHost host(0x02000000u);
            ps2x::iop::IopSubsystem subsystem(host);
            std::string error;
            t.IsTrue(subsystem.configure({"slus_201.84", 0u, 0u}, &error),
                     "RECVX profile should configure for TSNDDRV command testing");

            constexpr uint32_t kResponseAddress = 0x1000u;
            ps2x::iop::RpcRequest stateRequest{};
            stateRequest.sid = 1u;
            stateRequest.function = 0x12u;
            stateRequest.receive = {kResponseAddress, sizeof(uint32_t)};
            t.IsTrue(subsystem.handleRpc(stateRequest).handled,
                     "TSNDDRV should return its configured status buffer");
            const uint32_t statusAddress = host.readWord(kResponseAddress);
            t.IsTrue(statusAddress != 0u, "TSNDDRV status buffer should be allocated");

            constexpr int16_t kChecksum = 0x1234;
            t.IsTrue(host.writeGuest(0x01E0EF10u, &kChecksum, sizeof(kChecksum)),
                     "RECVX primary checksum binding should be writable in the fake guest");

            constexpr uint32_t kCommandAddress = 0x2000u;
            std::array<uint8_t, 8> command{};
            command[0] = 0x29u;
            command[1] = 0u;
            t.IsTrue(host.writeGuest(kCommandAddress, command.data(), command.size()),
                     "valid TSNDDRV command should be writable");

            ps2x::iop::RpcRequest commandRequest{};
            commandRequest.sid = 0u;
            commandRequest.function = 0u;
            commandRequest.send = {kCommandAddress, static_cast<uint32_t>(command.size())};
            t.IsTrue(subsystem.handleRpc(commandRequest).handled,
                     "TSNDDRV should handle the characterized command queue");

            int16_t writtenChecksum = 0;
            t.IsTrue(host.readGuest(statusAddress + 0x26u,
                                    &writtenChecksum,
                                    sizeof(writtenChecksum)),
                     "TSNDDRV SE checksum slot should be readable");
            t.Equals(writtenChecksum, kChecksum,
                     "valid port should mirror the profile-bound checksum table");

            constexpr uint32_t kPastStatusAddress = 0x44u;
            constexpr uint16_t kSentinel = 0xBEEFu;
            t.IsTrue(host.writeGuest(statusAddress + kPastStatusAddress,
                                     &kSentinel,
                                     sizeof(kSentinel)),
                     "sentinel after the status structure should be writable");
            command[1] = 0x0Fu;
            (void)host.writeGuest(kCommandAddress, command.data(), command.size());
            (void)subsystem.handleRpc(commandRequest);

            uint16_t sentinelAfter = 0u;
            (void)host.readGuest(statusAddress + kPastStatusAddress,
                                 &sentinelAfter,
                                 sizeof(sentinelAfter));
            t.Equals(sentinelAfter, kSentinel,
                     "invalid port must not overwrite memory past the 0x42-byte status structure");
        });

        tc.Run("RECVX reset clears CRI object maps without global state", [](TestCase &t)
        {
            FakeIopHost host(0x02000000u);
            ps2x::iop::IopSubsystem subsystem(host);
            std::string error;
            t.IsTrue(subsystem.configure({"slus_201.84", 0u, 0u}, &error),
                     "RECVX profile should configure");

            constexpr uint32_t kSendAddress = 0x2000u;
            constexpr uint32_t kReceiveAddress = 0x2100u;
            host.writeWord(kSendAddress + 0u, 0u);
            host.writeWord(kSendAddress + 4u, 0x4000u);
            host.writeWord(kSendAddress + 8u, 0x100u);

            ps2x::iop::RpcRequest request{};
            request.sid = 0x7D000000u;
            request.function = 0x422u;
            request.send = {kSendAddress, 12u};
            request.receive = {kReceiveAddress, 4u};
            t.IsTrue(subsystem.handleRpc(request).handled,
                     "SJRMT create should be emulated by the RECVX profile");

            ps2x::iop::DebugSnapshot snapshot = subsystem.debugSnapshot();
            const ps2x::iop::DebugService *service =
                findService(snapshot, "CRI DTX");
            if (!service)
            {
                t.Fail("CRI DTX service should be visible in the debug snapshot");
                return;
            }
            t.Equals(metricValue(*service, "sjrmt_objects"), uint64_t{1},
                     "created CRI object should be tracked by this instance");

            subsystem.reset();
            snapshot = subsystem.debugSnapshot();
            service = findService(snapshot, "CRI DTX");
            if (!service)
            {
                t.Fail("CRI DTX service should survive reset");
                return;
            }
            t.Equals(metricValue(*service, "sjrmt_objects"), uint64_t{0},
                     "reset should clear CRI object maps");
        });

        tc.Run("reset closes profile-owned host file handles", [](TestCase &t)
        {
            FakeIopHost host;
            host.hostFileContents["translated/test.bin"] = {0x10u, 0x20u, 0x30u};

            ps2x::iop::IopSubsystem subsystem(host);
            std::string error;
            t.IsTrue(subsystem.configure({"SLUS_205.78", 0u, 0u}, &error),
                     "LotR profile should configure for file lifecycle testing");

            constexpr uint32_t kPathAddress = 0x1000u;
            constexpr uint32_t kReceiveAddress = 0x1100u;
            constexpr char kPath[] = "test.bin";
            t.IsTrue(host.writeGuest(kPathAddress, kPath, sizeof(kPath)),
                     "fake guest path should be writable");

            ps2x::iop::RpcRequest request{};
            request.sid = 0x0000FF01u;
            request.function = 0x08u;
            request.send = {kPathAddress, sizeof(kPath)};
            request.receive = {kReceiveAddress, 8u};
            t.IsTrue(subsystem.handleRpc(request).handled,
                     "LotR CLFILE open should be handled");
            t.Equals(host.openHostFiles.size(), size_t{1},
                     "open RPC should retain one opaque host file handle");

            ps2x::iop::DebugSnapshot snapshot = subsystem.debugSnapshot();
            const ps2x::iop::DebugService *service =
                findService(snapshot, "CLFILE");
            if (!service)
            {
                t.Fail("LotR CLFILE service should be visible before reset");
                return;
            }
            t.Equals(metricValue(*service, "open_files"), uint64_t{1},
                     "debug state should report the open file");

            subsystem.reset();
            t.IsTrue(host.openHostFiles.empty(),
                     "reset should release every retained host file handle");
            t.Equals(host.closedHostFileHandles.size(), size_t{1},
                     "host close callback should run exactly once");
            snapshot = subsystem.debugSnapshot();
            service = findService(snapshot, "CLFILE");
            if (!service)
            {
                t.Fail("LotR CLFILE service should survive reset");
                return;
            }
            t.Equals(metricValue(*service, "open_files"), uint64_t{0},
                     "reset should clear the CLFILE handle registry");
        });

#if defined(PS2X_TEST_IOP_PLUGIN_DIR)
        tc.Run("plugin module remains loaded through instances and unloads after subsystem destruction", [](TestCase &t)
        {
            const std::filesystem::path pluginDirectory(PS2X_TEST_IOP_PLUGIN_DIR);
#if defined(_WIN32)
            const std::filesystem::path pluginPath =
                pluginDirectory / "ps2_iop_fake_plugin.dll";
#else
            const std::filesystem::path pluginPath =
                pluginDirectory / "ps2_iop_fake_plugin.so";
#endif
            t.IsFalse(pluginModuleIsLoaded(pluginPath),
                      "synthetic plugin should not be loaded before discovery");
            {
                FakeIopHost host;
                ps2x::iop::IopSubsystem subsystem(host);
                subsystem.setPluginSearchPaths({pluginDirectory});
                std::string error;
                t.IsTrue(subsystem.loadPlugins(&error),
                         "synthetic plugins should load for lifetime testing");
                t.IsTrue(subsystem.configure({"synthetic_iop_test.elf",
                                              kSyntheticEntryPoint,
                                              kSyntheticCrc32},
                                             &error),
                         "synthetic plugin instance should be created");
                t.IsTrue(pluginModuleIsLoaded(pluginPath),
                         "module must stay loaded while a profile instance exists");
            }
            t.IsFalse(pluginModuleIsLoaded(pluginPath),
                      "module should unload after profile destruction and catalog teardown");
        });

        tc.Run("plugin discovery matches all identity fields and dispatches through the host bridge", [](TestCase &t)
        {
            FakeIopHost host;
            ps2x::iop::IopSubsystem subsystem(host);
            const std::filesystem::path pluginDirectory(PS2X_TEST_IOP_PLUGIN_DIR);

            t.IsTrue(std::filesystem::is_directory(pluginDirectory),
                     "the synthetic IOP plugin directory should be staged by the test build");
            subsystem.setPluginSearchPaths({pluginDirectory});

            std::string error;
            t.IsTrue(subsystem.loadPlugins(&error), "synthetic IOP plugin discovery should succeed");
            ps2x::iop::DebugSnapshot discoverySnapshot = subsystem.debugSnapshot();
            t.IsTrue(containsDiagnostic(discoverySnapshot, "loaded 4 profile(s)"),
                     "plugin discovery diagnostics should report all accepted synthetic profiles");
            t.IsTrue(containsDiagnostic(discoverySnapshot, "too many SIDs"),
                     "an invalid profile descriptor should be ignored with a diagnostic");
            t.IsTrue(containsDiagnostic(discoverySnapshot, "bad_abi"),
                     "an ABI-incompatible plugin should be ignored with a diagnostic");
            t.IsTrue(containsDiagnostic(discoverySnapshot, "incompatible ABI"),
                     "the incompatible-plugin diagnostic should explain the ABI failure");
            t.IsTrue(containsDiagnostic(discoverySnapshot, "missing_symbol"),
                     "a plugin without the query symbol should be ignored with a diagnostic");
            t.IsTrue(containsDiagnostic(discoverySnapshot, "missing ps2x_iop_query_v1"),
                     "the missing-symbol diagnostic should name the required entry point");

            auto expectNoProfile = [&](const ps2x::iop::GameIdentity &identity, const std::string &reason) {
                error.clear();
                t.IsTrue(subsystem.configure(identity, &error), "mismatching plugin identity should configure core-only services");
                const ps2x::iop::DebugSnapshot snapshot = subsystem.debugSnapshot();
                t.IsTrue(snapshot.activeProfile.empty(), reason);

                ps2x::iop::RpcRequest request{};
                request.sid = kSyntheticSid;
                request.function = kSyntheticFunction;
                t.IsFalse(subsystem.handleRpc(request).handled,
                          "a mismatching profile must not expose its synthetic SID");
            };

            expectNoProfile({"different.elf", kSyntheticEntryPoint, kSyntheticCrc32},
                            "a different ELF basename should not match the plugin profile");
            expectNoProfile({"synthetic_iop_test.elf", kSyntheticEntryPoint + 4u, kSyntheticCrc32},
                            "a different entry point should not match the plugin profile");
            expectNoProfile({"synthetic_iop_test.elf", kSyntheticEntryPoint, kSyntheticCrc32 ^ 1u},
                            "a different CRC32 should not match the plugin profile");

            error.clear();
            t.IsTrue(subsystem.configure({"synthetic_iop_test.elf", kSyntheticEntryPoint, kSyntheticCrc32}, &error),
                     "the synthetic ELF identity should activate the plugin profile");

            ps2x::iop::DebugSnapshot snapshot = subsystem.debugSnapshot();
            t.Equals(snapshot.activeProfile, std::string("synthetic-test-profile"),
                     "debug snapshot should expose the active plugin profile id");
            t.Equals(snapshot.activeProvider, std::string("ps2x-test-plugin"),
                     "debug snapshot should expose the plugin provider name");
            const ps2x::iop::DebugService *service = findService(snapshot, "synthetic-test-profile");
            if (!service)
            {
                t.Fail("debug snapshot should include the synthetic profile service");
                return;
            }
            t.IsTrue(service->profileSpecific, "plugin service should be marked profile-specific");
            t.IsTrue(std::find(service->sids.begin(), service->sids.end(), kSyntheticSid) != service->sids.end(),
                     "plugin service should advertise its synthetic SID");
            t.Equals(metricValue(*service, "reset_generation"), uint64_t{1},
                     "profile configuration should reset a new plugin instance once");

            ps2x::iop::RpcAbiRequest abiRequest{};
            abiRequest.boundSid = kSyntheticSid;
            abiRequest.function = kSyntheticFunction;
            abiRequest.registers.plausible = true;
            abiRequest.stack.plausible = true;
            t.Equals(subsystem.selectRpcAbi(abiRequest), ps2x::iop::RpcAbi::Stack,
                     "plugin should be able to select the stack RPC ABI");
            abiRequest.function = kSyntheticFunction + 1u;
            t.Equals(subsystem.selectRpcAbi(abiRequest), ps2x::iop::RpcAbi::RuntimeDefault,
                     "plugin ABI selection should fall back for unrelated functions");

            constexpr uint32_t kSendAddress = 0x1000u;
            constexpr uint32_t kReceiveAddress = 0x1100u;
            constexpr uint32_t kInput = 0x1234ABCDu;
            t.IsTrue(host.writeWord(kSendAddress, kInput), "fake host should seed the plugin send buffer");
            t.IsTrue(host.writeWord(kReceiveAddress, 0u), "fake host should clear the plugin receive buffer");

            ps2x::iop::RpcRequest request{};
            request.callToken = 0x1122334455667788ull;
            request.sid = kSyntheticSid;
            request.function = kSyntheticFunction;
            request.send = {kSendAddress, sizeof(uint32_t)};
            request.receive = {kReceiveAddress, sizeof(uint32_t)};
            const ps2x::iop::RpcResult result = subsystem.handleRpc(request);

            t.IsTrue(result.handled, "matching synthetic SID/function should dispatch to the plugin");
            t.Equals(result.resultAddress, kReceiveAddress, "plugin should return its receive-buffer address");
            t.IsTrue(result.signalNowaitCompletion, "plugin should request nowait completion signaling");
            t.Equals(result.callbackPolicy, ps2x::iop::CallbackPolicy::Suppress,
                     "plugin should be able to suppress the runtime callback");
            t.Equals(host.readWord(kReceiveAddress), kInput ^ kResponseXor,
                     "plugin should read and write guest memory through the IopHost bridge");

            ps2x::iop::RpcRequest unknownRequest{};
            unknownRequest.sid = 0xDEADC0DEu;
            unknownRequest.function = kSyntheticFunction;
            t.IsFalse(subsystem.handleRpc(unknownRequest).handled,
                      "unknown SID should remain unhandled while a plugin profile is active");

            constexpr uint32_t kCoreCollisionReceiveAddress = 0x1200u;
            ps2x::iop::RpcRequest collisionRequest{};
            collisionRequest.sid = kCoreCollisionSid;
            collisionRequest.function = kCoreCollisionFunction;
            collisionRequest.receive = {kCoreCollisionReceiveAddress, sizeof(uint32_t)};
            const ps2x::iop::RpcResult collisionResult = subsystem.handleRpc(collisionRequest);
            t.IsTrue(collisionResult.handled,
                     "a profile service should take precedence over a core service for the same SID");
            t.Equals(host.readWord(kCoreCollisionReceiveAddress), kCoreCollisionResponse,
                     "the profile collision route should reach the plugin implementation");

            subsystem.onSifTransfer({ps2x::iop::SifTransferKind::SetDma,
                                     ps2x::iop::SifTransferPhase::AfterCopy,
                                     kSendAddress,
                                     kReceiveAddress,
                                     sizeof(uint32_t)});
            snapshot = subsystem.debugSnapshot();
            service = findService(snapshot, "synthetic-test-profile");
            if (!service)
            {
                t.Fail("synthetic profile service should remain visible after dispatch");
                return;
            }
            t.Equals(metricValue(*service, "rpc_calls"), uint64_t{2},
                     "plugin debug metrics should count dispatched RPCs");
            t.Equals(metricValue(*service, "sif_transfers"), uint64_t{1},
                     "plugin debug metrics should count SIF transfer hooks");

            subsystem.reset();
            snapshot = subsystem.debugSnapshot();
            service = findService(snapshot, "synthetic-test-profile");
            if (!service)
            {
                t.Fail("synthetic profile service should remain visible after reset");
                return;
            }
            t.Equals(metricValue(*service, "reset_generation"), uint64_t{2},
                     "explicit subsystem reset should reach the plugin instance");
            t.Equals(metricValue(*service, "rpc_calls"), uint64_t{0},
                     "plugin reset should clear per-instance RPC state");
            t.Equals(metricValue(*service, "sif_transfers"), uint64_t{0},
                     "plugin reset should clear per-instance transfer state");

            error.clear();
            t.IsFalse(subsystem.configure({"synthetic_duplicate.elf", kSyntheticEntryPoint, kSyntheticCrc32}, &error),
                      "duplicate SIDs inside one profile layer should reject configuration");
            t.IsTrue(error.find("duplicate IOP SID") != std::string::npos,
                     "duplicate-SID failure should clearly identify the registry conflict");

            error.clear();
            t.IsFalse(subsystem.configure({"slus_201.84", kSyntheticEntryPoint, kSyntheticCrc32}, &error),
                      "equally specific built-in and plugin matchers should be ambiguous");
            t.IsTrue(error.find("ambiguous IOP profiles") != std::string::npos,
                     "ambiguous profile selection should fail clearly");

            error.clear();
            t.IsTrue(subsystem.configure({"slus_201.84",
                                          kSpecificRecvXEntryPoint,
                                          kSyntheticCrc32},
                                         &error),
                     "a more-specific matcher should win over a lower-specificity tie");
            t.Equals(subsystem.debugSnapshot().activeProfile,
                     std::string("synthetic-specific-recvx-profile"),
                     "the most specific plugin profile should be selected");

            error.clear();
            t.IsTrue(subsystem.configure({"different.elf", kSyntheticEntryPoint, kSyntheticCrc32}, &error),
                     "switching to an unmatched ELF should destroy the active plugin profile");
            t.IsTrue(host.hasLog("fake-plugin-destroy"),
                     "plugin profile destroy callback should run when the active profile is replaced");
            t.IsTrue(subsystem.debugSnapshot().activeProfile.empty(),
                     "switching to an unmatched ELF should leave no active profile");
        });
#endif
    });
}
