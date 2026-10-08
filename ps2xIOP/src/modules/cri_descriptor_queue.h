#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <utility>

namespace ps2x::iop::detail {
struct CriChunk {
    uint32_t address = 0;
    uint32_t size = 0;
    bool operator==(const CriChunk&) const = default;
};
// Retail CRI SJU: four descriptor lists sharing a finite node pool. Mode 1
// permits contiguous merging and partial reads. Other modes retain records.
class CriDescriptorQueue {
public:
    CriDescriptorQueue(uint32_t capacity, uint8_t mode) : capacity_(capacity), split_(mode == 1) {}
    void reset() { for(auto& q : lines_) q.clear(); used_ = 0; }
    bool put(uint32_t line, CriChunk chunk, bool prepend = false) {
        if(line >= lines_.size()) return false;
        if(!chunk.address || !chunk.size) return true;
        if(chunk.size > 0x7fffffffu || chunk.address > UINT32_MAX - chunk.size) return false;
        auto& q = lines_[line];
        if(split_ && !q.empty()) {
            auto& edge = prepend ? q.front() : q.back();
            const bool adjacent = prepend ? chunk.address + chunk.size == edge.address
                                          : edge.address + edge.size == chunk.address;
            if(adjacent && uint64_t(edge.size) + chunk.size <= 0x7fffffffu) {
                if(prepend) edge.address = chunk.address;
                edge.size += chunk.size;
                return true;
            }
        }
        if(used_ == capacity_) return false;
        if(prepend) q.push_front(chunk); else q.push_back(chunk);
        ++used_;
        return true;
    }
    CriChunk get(uint32_t line, uint32_t requested) {
        if(line >= lines_.size() || !requested || requested > 0x7fffffffu) return {};
        auto& q = lines_[line];
        if(q.empty()) return {};
        auto result = q.front();
        if(result.size <= requested) { q.pop_front(); --used_; return result; }
        if(!split_) return {};
        result.size = requested;
        q.front().address += requested;
        q.front().size -= requested;
        return result;
    }
    std::pair<bool,uint32_t> canGet(uint32_t line, uint32_t requested) const {
        if(line >= lines_.size() || lines_[line].empty()) return {false,0};
        const auto size = lines_[line].front().size;
        return {split_ ? requested <= size : requested == size, size};
    }
    uint32_t bytes(uint32_t line) const {
        uint32_t n = 0;
        if(line < lines_.size()) for(auto chunk : lines_[line]) n += chunk.size;
        return n;
    }
    const std::deque<CriChunk>& chunks(uint32_t line) const { return lines_.at(line); }
    uint32_t usedDescriptors() const { return used_; }
private:
    uint32_t capacity_, used_ = 0;
    bool split_;
    std::array<std::deque<CriChunk>,4> lines_;
};
}
