#pragma once

#include "ps2_runtime.h"
#include "ps2_iop_host.h"
#include "ps2x/iop/iop_subsystem.h"

#include <utility>

class PS2IopTransport
{
public:
    static bool configureForTesting(
        PS2Runtime *runtime,
        const ps2x::iop::GameIdentity &identity,
        std::string *error = nullptr)
    {
        return runtime && runtime->m_iopSubsystem &&
               runtime->m_iopSubsystem->configure(identity, error);
    }

    [[nodiscard]] static ps2x::iop::RpcAbi selectRpcAbi(
        const PS2Runtime *runtime,
        const ps2x::iop::RpcAbiRequest &request)
    {
        return runtime
                   ? runtime->selectIopRpcAbi(request)
                   : ps2x::iop::RpcAbi::RuntimeDefault;
    }

    [[nodiscard]] static ps2x::iop::RpcResult handleRpc(
        PS2Runtime *runtime,
        uint8_t *rdram,
        R5900Context *context,
        ps2x::iop::RpcRequest request)
    {
        return runtime
                   ? runtime->handleIopRpc(rdram, context, std::move(request))
                   : ps2x::iop::RpcResult{};
    }

    static void notifyTransfer(
        PS2Runtime *runtime,
        uint8_t *rdram,
        const ps2x::iop::SifTransfer &transfer)
    {
        if (runtime)
        {
            runtime->notifyIopSifTransfer(rdram, transfer);
        }
    }

    static bool transferSoundMemory(PS2Runtime* runtime,uint8_t* rdram,R5900Context* context,
                                    uint32_t iopAddress,uint32_t soundAddress,uint32_t bytes,bool readBack)
    {
        if(!runtime || !runtime->m_iopHost || bytes>0x200000 || soundAddress>0x200000-bytes)
            return false;
        auto& host=*runtime->m_iopHost;
        auto scope=host.enterCall(context,rdram);
        std::vector<uint8_t> data(bytes);
        if(readBack)
            return host.readSoundMemory(soundAddress,data.data(),bytes) && host.writeGuest(iopAddress,data.data(),bytes);
        return host.readGuest(iopAddress,data.data(),bytes) && host.writeSoundMemory(soundAddress,data.data(),bytes);
    }

    static bool readSoundMemory(PS2Runtime* runtime,uint32_t address,void* destination,size_t size)
    {return runtime && runtime->m_iopHost && runtime->m_iopHost->readSoundMemory(address,destination,size);}

    static void resetSoundMemory(PS2Runtime* runtime)
    {if(runtime && runtime->m_iopHost)runtime->m_iopHost->resetSoundMemory();}

    static void reset(PS2Runtime *runtime)
    {
        if (runtime)
        {
            runtime->resetIop();
        }
    }
};
