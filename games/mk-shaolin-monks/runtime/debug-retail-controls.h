#pragma once
#include "ps2_runtime.h"
#include <cstring>

void mk_recovered_001d32e0_0x1d32e0(uint8_t*, R5900Context*, PS2Runtime*);

// Exact retail identity is enforced by the installing game override.
// Exercise the original state's opt-out branch, restoring its flag afterwards.
inline void mkDebugSkipIntro(uint8_t* ram, R5900Context* context, PS2Runtime* runtime) {
    struct FlagScope {
        uint8_t* address;
        uint32_t previous;
        explicit FlagScope(uint8_t* p):address(p) {
            std::memcpy(&previous,p,4);
            const uint32_t enabled=1;
            std::memcpy(p,&enabled,4);
        }
        ~FlagScope(){std::memcpy(address,&previous,4);}
    } flag(ram+0x513a04);
    mk_recovered_001d32e0_0x1d32e0(ram,context,runtime);
}
