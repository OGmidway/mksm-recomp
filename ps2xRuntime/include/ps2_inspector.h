#pragma once
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>
class PS2Runtime;
struct R5900Context;
// Read-only executor-boundary snapshots. No external guest mutation or Python dependency.
class PS2Inspector {
public:
    PS2Inspector();
    bool due(bool eventBoundary = false);
    void capture(PS2Runtime&, const R5900Context&, const uint8_t*, const char* state);
private:
    struct Watch { std::string name; uint32_t address, size; bool indirect; };
    std::filesystem::path m_path;
    std::string m_session;
    std::vector<Watch> m_watches;
    std::deque<std::string> m_history;
    std::chrono::steady_clock::time_point m_next{};
    uint64_t m_sequence=0;
    unsigned m_gate=0;
    bool m_errorLogged=false;
};
