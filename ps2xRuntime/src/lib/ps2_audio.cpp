#include "runtime/ps2_audio.h"
#include "runtime/ps2_pcm_queue.h"
#include "runtime/ps2_memory.h"
#include "ps2_host_backend.h"
#include <cstring>
#include <vector>
#include <atomic>
#include <utility>
#include <cstdlib>
#include <fstream>

namespace {
// raylib callbacks carry no user data. Bounded slots own shared queue lifetimes;
// callbacks never touch the runtime, guest RAM, or the backend control mutex.
constexpr size_t PcmSlots=32;
std::array<std::atomic<std::shared_ptr<PS2PcmQueue>>,PcmSlots> pcmSlots;
template<size_t I> void pcmCallback(void* output,unsigned frames) {
    auto queue=pcmSlots[I].load();
    std::span<int16_t> samples(static_cast<int16_t*>(output),size_t(frames)*2);
    if(queue)queue->pull(samples);else std::fill(samples.begin(),samples.end(),0);
}
template<size_t... I> constexpr auto pcmCallbacks(std::index_sequence<I...>) {
    return std::array<AudioCallback,sizeof...(I)>{pcmCallback<I>...};
}
constexpr auto PcmCallbacks=pcmCallbacks(std::make_index_sequence<PcmSlots>{});
}

namespace
{
    std::vector<uint8_t> buildWavFromPcm(const int16_t *pcm, size_t sampleCount, uint32_t sampleRate)
    {
        const uint32_t dataSize = static_cast<uint32_t>(sampleCount * 2);
        const uint32_t fileSize = 36 + dataSize;
        std::vector<uint8_t> wav(8 + fileSize);

        uint8_t *p = wav.data();
        p[0] = 'R';
        p[1] = 'I';
        p[2] = 'F';
        p[3] = 'F';
        p[4] = static_cast<uint8_t>(fileSize);
        p[5] = static_cast<uint8_t>(fileSize >> 8);
        p[6] = static_cast<uint8_t>(fileSize >> 16);
        p[7] = static_cast<uint8_t>(fileSize >> 24);
        p[8] = 'W';
        p[9] = 'A';
        p[10] = 'V';
        p[11] = 'E';
        p[12] = 'f';
        p[13] = 'm';
        p[14] = 't';
        p[15] = ' ';
        p[16] = 16;
        p[17] = 0;
        p[18] = 0;
        p[19] = 0;
        p[20] = 1;
        p[21] = 0;
        p[22] = 1;
        p[23] = 0;
        p[24] = static_cast<uint8_t>(sampleRate);
        p[25] = static_cast<uint8_t>(sampleRate >> 8);
        p[26] = static_cast<uint8_t>(sampleRate >> 16);
        p[27] = static_cast<uint8_t>(sampleRate >> 24);
        const uint32_t byteRate = sampleRate * 2;
        p[28] = static_cast<uint8_t>(byteRate);
        p[29] = static_cast<uint8_t>(byteRate >> 8);
        p[30] = static_cast<uint8_t>(byteRate >> 16);
        p[31] = static_cast<uint8_t>(byteRate >> 24);
        p[32] = 2;
        p[33] = 0;
        p[34] = 16;
        p[35] = 0;
        p[36] = 'd';
        p[37] = 'a';
        p[38] = 't';
        p[39] = 'a';
        p[40] = static_cast<uint8_t>(dataSize);
        p[41] = static_cast<uint8_t>(dataSize >> 8);
        p[42] = static_cast<uint8_t>(dataSize >> 16);
        p[43] = static_cast<uint8_t>(dataSize >> 24);
        std::memcpy(p + 44, pcm, dataSize);
        return wav;
    }
}

namespace ps2_vag
{
    bool decode(const uint8_t *data, uint32_t sizeBytes,
                std::vector<int16_t> &outPcm, uint32_t &outSampleRate);
}

struct PS2AudioBackend::Impl
{
    struct TrackedSound
    {
        Sound snd;
        uint32_t sampleKey;
    };
    std::vector<TrackedSound> activeSounds;
    struct PcmStream {AudioStream stream{};std::shared_ptr<PS2PcmQueue> queue;size_t slot;uint32_t baseRate,rate;bool playing;};
    std::unordered_map<uint32_t,PcmStream> pcm;
    std::ofstream pcmCapture;
    size_t pcmCaptureBytes=0;
    void capture(uint32_t key,uint32_t rate,std::span<const int16_t> stereo) {
        // Little-endian PCMC records: key, sample rate, stereo frame count, s16 samples.
        constexpr size_t cap=2*1024*1024;
        const size_t bytes=16+stereo.size()*2;
        if(!pcmCapture.is_open() || !pcmCapture.good() || bytes>cap-pcmCaptureBytes)return;
        const uint32_t header[]={0x434d4350u,key,rate,static_cast<uint32_t>(stereo.size()/2)};
        for(auto word:header)for(unsigned shift=0;shift<32;shift+=8)pcmCapture.put(char(word>>shift));
        for(auto sample:stereo) {const auto word=static_cast<uint16_t>(sample);pcmCapture.put(char(word));pcmCapture.put(char(word>>8));}
        pcmCaptureBytes+=bytes;
        pcmCapture.flush(); // A bounded diagnostic process can be terminated without destructors.
    }
};

PS2AudioBackend::PS2AudioBackend() : m_impl(std::make_unique<Impl>())
{
    if(const char* path=std::getenv("PS2_INSPECTOR_PCM");path && *path)
        m_impl->pcmCapture.open(path,std::ios::binary|std::ios::trunc);
}

PS2AudioBackend::~PS2AudioBackend()
{
    if (m_impl)
        stopAll();
}

void PS2AudioBackend::onVagTransfer(const uint8_t *rdram, uint32_t srcAddr, uint32_t sizeBytes)
{
    if (!rdram || sizeBytes < 48)
        return;

    const uint32_t physAddr = srcAddr & PS2_RAM_MASK;
    if (physAddr + sizeBytes > PS2_RAM_SIZE)
        return;

    std::vector<int16_t> pcm;
    uint32_t sampleRate = 44100;
    if (!ps2_vag::decode(rdram + physAddr, sizeBytes, pcm, sampleRate))
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    DecodedSample sample;
    sample.pcm = std::move(pcm);
    sample.sampleRate = sampleRate;
    m_sampleBank[physAddr] = std::move(sample);
    m_mostRecentSampleKey = physAddr;
}

void PS2AudioBackend::onVagTransferFromBuffer(const uint8_t *data, uint32_t sizeBytes, uint32_t keyAddr)
{
    if (!data || sizeBytes < 48)
        return;

    std::vector<int16_t> pcm;
    uint32_t sampleRate = 44100;
    if (!ps2_vag::decode(data, sizeBytes, pcm, sampleRate))
        return;

    const uint32_t physAddr = keyAddr & PS2_RAM_MASK;
    std::lock_guard<std::mutex> lock(m_mutex);
    DecodedSample sample;
    sample.pcm = std::move(pcm);
    sample.sampleRate = sampleRate;
    m_sampleBank[physAddr] = sample;
    m_mostRecentSampleKey = physAddr;
    m_loadOrderSamples.push_back(std::move(sample));
    m_loadOrderSampleKeys.push_back(physAddr);
    constexpr size_t kMaxLoadOrderSamples = 32;
    if (m_loadOrderSamples.size() > kMaxLoadOrderSamples)
    {
        m_loadOrderSamples.erase(m_loadOrderSamples.begin());
        m_loadOrderSampleKeys.erase(m_loadOrderSampleKeys.begin());
    }
}

namespace
{
    constexpr uint32_t LIBSD_CMD_SET_VOICE = 0x8010u;
}

void PS2AudioBackend::onSoundCommand(uint32_t sid, uint32_t rpcNum,
                                     const uint8_t *sendBuf, uint32_t sendSize,
                                     uint8_t *recvBuf, uint32_t recvSize)
{
    if (sid != 0x80000701u)
        return;

    if ((rpcNum == LIBSD_CMD_SET_VOICE || (rpcNum & 0xFF00u) == 0x8100u) &&
        sendBuf && sendSize >= 20)
    {
        uint32_t sampleAddr = 0;
        uint32_t voiceIndex = 0xFFFFFFFFu;
        for (int vo = 4; vo >= 0 && voiceIndex == 0xFFFFFFFFu; vo -= 4)
        {
            if (vo < static_cast<int>(sendSize))
            {
                uint32_t v = 0;
                std::memcpy(&v, sendBuf + vo, sizeof(v));
                if (v < 24u)
                    voiceIndex = v;
            }
        }

        constexpr uint32_t kMinPlausibleAddr = 0x1000u;
        for (int off = 12; off <= 24 && sampleAddr == 0; off += 4)
        {
            if (sendSize >= static_cast<uint32_t>(off + 4))
            {
                uint32_t cand = 0;
                std::memcpy(&cand, sendBuf + off, sizeof(cand));
                if (cand >= kMinPlausibleAddr && (cand <= PS2_RAM_MASK || (cand & ~PS2_RAM_MASK) == 0))
                    sampleAddr = cand;
            }
        }
        if (sampleAddr == 0)
            sampleAddr = m_mostRecentSampleKey;

        float pitch = 1.0f;
        if (sendSize >= 12)
        {
            uint16_t pitchHalf = 0;
            std::memcpy(&pitchHalf, sendBuf + 8, sizeof(pitchHalf));
            if (pitchHalf != 0)
                pitch = 4096.0f / static_cast<float>(pitchHalf);
        }
        play(sampleAddr, pitch, 1.0f, voiceIndex);
    }
}

void PS2AudioBackend::play(uint32_t sampleAddr, float pitch, float volume, uint32_t voiceIndex)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    DecodedSample *sampleToPlay = nullptr;
    uint32_t sampleKey = 0;

    auto it = m_sampleBank.find(sampleAddr & PS2_RAM_MASK);
    if (it != m_sampleBank.end())
    {
        sampleToPlay = &it->second;
        sampleKey = it->first;
    }
    else if (voiceIndex != 0xFFFFFFFFu &&
             voiceIndex < m_loadOrderSamples.size() &&
             voiceIndex < m_loadOrderSampleKeys.size())
    {
        sampleToPlay = &m_loadOrderSamples[voiceIndex];
        sampleKey = m_loadOrderSampleKeys[voiceIndex];
    }
    else
    {
        it = m_sampleBank.find(m_mostRecentSampleKey);
        if (it == m_sampleBank.end())
            return;
        sampleToPlay = &it->second;
        sampleKey = it->first;
    }
    if (!sampleToPlay || sampleToPlay->pcm.empty())
        return;

    const bool isBgm = (sampleToPlay->pcm.size() > static_cast<size_t>(sampleToPlay->sampleRate * 5));
    playDecodedSample(sampleKey, *sampleToPlay, pitch, volume, isBgm);
}

void PS2AudioBackend::pruneFinishedSounds()
{
#if defined(PLATFORM_VITA)
    return;
#else
    auto &sounds = m_impl->activeSounds;
    auto it = sounds.begin();
    while (it != sounds.end())
    {
        if (!IsSoundPlaying(it->snd))
        {
            UnloadSound(it->snd);
            it = sounds.erase(it);
        }
        else
        {
            ++it;
        }
    }
#endif
}

void PS2AudioBackend::playDecodedSample(uint32_t sampleKey, DecodedSample &sample, float pitch, float volume,
                                        bool isBgm)
{
#if defined(PLATFORM_VITA)
    (void)sampleKey;
    (void)sample;
    (void)pitch;
    (void)volume;
    (void)isBgm;
    return;
#else
    if (!m_audioReady || sample.pcm.empty())
        return;

    pruneFinishedSounds();

    for (const auto &t : m_impl->activeSounds)
    {
        if (t.sampleKey == sampleKey && IsSoundPlaying(t.snd))
            return;
    }

    auto &sounds = m_impl->activeSounds;
    if (isBgm)
    {
        for (auto it = sounds.begin(); it != sounds.end();)
        {
            if (IsSoundPlaying(it->snd))
            {
                StopSound(it->snd);
                UnloadSound(it->snd);
                it = sounds.erase(it);
            }
            else
                ++it;
        }
    }

    constexpr int kMaxConcurrentSounds = 4;
    while (static_cast<int>(sounds.size()) >= kMaxConcurrentSounds)
    {
        StopSound(sounds.front().snd);
        UnloadSound(sounds.front().snd);
        sounds.erase(sounds.begin());
    }

    std::vector<uint8_t> wav = buildWavFromPcm(sample.pcm.data(), sample.pcm.size(), sample.sampleRate);
    Wave wave = LoadWaveFromMemory(".wav", wav.data(), static_cast<int>(wav.size()));
    if (wave.frameCount <= 0)
        return;
    Sound snd = LoadSoundFromWave(wave);
    UnloadWave(wave);
    SetSoundPitch(snd, pitch);
    SetSoundVolume(snd, volume);
    m_impl->activeSounds.push_back({snd, sampleKey});
    PlaySound(snd);
#endif
}

void PS2AudioBackend::stop(uint32_t voiceId)
{
    (void)voiceId;
}

void PS2AudioBackend::stopAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
#if defined(PLATFORM_VITA)
    return;
#else
    for(auto& [key,p]:m_impl->pcm) {
        StopAudioStream(p.stream);UnloadAudioStream(p.stream);pcmSlots[p.slot].store(nullptr);
    }
    m_impl->pcm.clear();
    for (auto &t : m_impl->activeSounds)
    {
        StopSound(t.snd);
        UnloadSound(t.snd);
    }
    m_impl->activeSounds.clear();
#endif
}

bool PS2AudioBackend::pcmConfigure(uint32_t key,uint32_t rate,bool playing) {
    std::lock_guard lock(m_mutex);
#if defined(PLATFORM_VITA)
    return false;
#else
    if(!m_audioReady || !rate || rate>192000)return false;
    auto it=m_impl->pcm.find(key);
    if(it==m_impl->pcm.end()) {
        if(!playing)return false;
        auto queue=std::make_shared<PS2PcmQueue>();size_t slot=0;
        for(;slot<PcmSlots;++slot) {
            std::shared_ptr<PS2PcmQueue> empty;
            if(pcmSlots[slot].compare_exchange_strong(empty,queue))break;
        }
        if(slot==PcmSlots)return false;
        auto stream=LoadAudioStream(rate,16,2);
        if(!stream.buffer) {pcmSlots[slot].store(nullptr);return false;}
        SetAudioStreamCallback(stream,PcmCallbacks[slot]);
        it=m_impl->pcm.emplace(key,Impl::PcmStream{stream,queue,slot,rate,rate,false}).first;
        PlayAudioStream(stream);
    }
    auto& p=it->second;
    if(p.rate!=rate) {SetAudioStreamPitch(p.stream,float(rate)/p.baseRate);p.rate=rate;}
    if(p.playing!=playing) {
        if(playing)ResumeAudioStream(p.stream);else PauseAudioStream(p.stream);
        p.playing=playing;
    }
    return true;
#endif
}
bool PS2AudioBackend::pcmSubmit(uint32_t key,std::span<const int16_t> stereo) {
    std::lock_guard lock(m_mutex);
    const auto it=m_impl->pcm.find(key);
    if(it==m_impl->pcm.end() || !it->second.queue->push(stereo))return false;
    m_impl->capture(key,it->second.rate,stereo);
    return true;
}
uint64_t PS2AudioBackend::pcmConsumed(uint32_t key) const {
    std::lock_guard lock(m_mutex);
    const auto it=m_impl->pcm.find(key);
    return it==m_impl->pcm.end()?0:it->second.queue->consumed();
}
void PS2AudioBackend::pcmClose(uint32_t key) {
    std::lock_guard lock(m_mutex);
    const auto it=m_impl->pcm.find(key);if(it==m_impl->pcm.end())return;
#if !defined(PLATFORM_VITA)
    StopAudioStream(it->second.stream);UnloadAudioStream(it->second.stream);
#endif
    pcmSlots[it->second.slot].store(nullptr);m_impl->pcm.erase(it);
}
