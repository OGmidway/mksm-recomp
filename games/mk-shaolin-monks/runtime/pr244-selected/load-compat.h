#pragma once
#include "ps2_runtime_macros.h"
#ifndef SET_GPR_ZE32
#define SET_GPR_ZE32(ctx_ptr, reg_idx, val)                               \
    do                                                                    \
    {                                                                     \
        if ((reg_idx) != 0)                                               \
        {                                                                 \
            __m128i _newVal = _mm_cvtsi64_si128((int64_t)(uint32_t)(val)); \
            Ps2SetGprLow64(ctx_ptr, reg_idx, _newVal);                    \
        }                                                                 \
    } while (0)


#endif
