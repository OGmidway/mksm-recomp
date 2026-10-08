#pragma once

#include "ps2_stubs.h"

namespace ps2_stubs
{
    // SDK revisions place these private fields at different offsets. A game
    // adapter can retain its original guest initialization and select its ABI.
    struct MpegPictureGuestLayout
    {
        uint32_t mode = 0xb0, destination = 0xd8, count = 0xe4;
        uint32_t progress = 0xdc, flags = 0xe0;
    };
    void initializeMpegPlaybackState(uint32_t mpegAddr, MpegPictureGuestLayout layout);
    size_t getMpegQueuedPictureCount(uint32_t mpegAddr);
    // For adapters with their own elementary-stream producer. Call only after
    // its authoritative EOF and all buffered bytes have been submitted.
    void finishMpegElementaryStream(uint32_t mpegAddr, PS2Runtime *runtime = nullptr);
    struct MpegDebugSnapshot
    {
        bool initialized = false;
        uint64_t players = 0, decoders = 0, queuedPictures = 0, picturesServed = 0;
        uint64_t playersWithInput = 0, waitingForSequence = 0, feedCalls = 0;
        uint64_t cdBytesProduced = 0, cdBytesDemuxed = 0;
    };
    MpegDebugSnapshot getMpegDebugSnapshot();
    void resetMpegStubState();
    void enqueueMpegDecodedFrameForTesting(uint32_t mpegAddr);
    void notifyMpegCdStreamStart(PS2Runtime *runtime = nullptr);
    void notifyMpegCdStreamDataProduced(uint32_t byteCount, bool endOfStream);
    void notifyMpegCdStreamEof(PS2Runtime *runtime = nullptr);
    void sceMpegFlush(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegAddBs(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegAddCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegAddStrCallback(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegClearRefBuff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegCreate(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegDelete(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegDemuxPss(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegDemuxPssRing(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegDispCenterOffX(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegDispCenterOffY(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegDispHeight(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegDispWidth(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegGetDecodeMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegGetPicture(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegGetPictureRAW8(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegGetPictureRAW8xy(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegInit(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegIsEnd(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegIsRefBuffEmpty(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegReset(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegResetDefaultPtsGap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegSetDecodeMode(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegSetDefaultPtsGap(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
    void sceMpegSetImageBuff(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime);
}
