#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <span>

// Only samples pulled by the device count as consumed. Underrun silence must
// never release guest-owned buffers or advance the guest audio clock.
class PS2PcmQueue {
public:
    static constexpr size_t Capacity = 4096;
    bool push(std::span<const int16_t> stereo) {
        std::lock_guard lock(mutex_);
        if(stereo.size()%2 || stereo.size()/2>Capacity-count_)return false;
        for(size_t i=0;i<stereo.size()/2;++i) {
            const auto slot=(head_+count_+i)%Capacity;
            samples_[slot*2]=stereo[i*2];samples_[slot*2+1]=stereo[i*2+1];
        }
        count_+=stereo.size()/2;return true;
    }
    void pull(std::span<int16_t> stereo) {
        std::lock_guard lock(mutex_);
        std::fill(stereo.begin(),stereo.end(),0);
        const auto frames=std::min(stereo.size()/2,count_);
        for(size_t i=0;i<frames;++i) {
            const auto slot=(head_+i)%Capacity;
            stereo[i*2]=samples_[slot*2];stereo[i*2+1]=samples_[slot*2+1];
        }
        head_=(head_+frames)%Capacity;count_-=frames;consumed_+=frames;
    }
    uint64_t consumed() const {std::lock_guard lock(mutex_);return consumed_;}
private:
    mutable std::mutex mutex_;
    std::array<int16_t,Capacity*2> samples_{};
    size_t head_=0,count_=0;uint64_t consumed_=0;
};
