#include "ps2_inspector.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "runtime/gs/gs_frontend.h"
#include "Kernel/Stubs/Pad.h"
#include "Kernel/Stubs/MPEG.h"
#include "Kernel/Syscalls/Helpers/State.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif
namespace {
std::string quote(const std::string& value) {
    std::ostringstream out;out << '"';
    for(unsigned char c:value) {
        if(c=='"' || c=='\\') out << '\\' << char(c);
        else if(c<32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
        else out << char(c);
    }
    out << '"'; return out.str();
}
uint64_t nowMs() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
void readDiagnosticPad(const std::string& session) {
    const char* path=std::getenv("PS2_INSPECTOR_PAD_FILE");
    if(!path || !*path)return;
    // Separate opt-in host input, never guest-memory writes. A short lease
    // releases keys if the Python driver exits or stops refreshing the file.
    std::ifstream input(path);std::string token;uint64_t expires=0;
    unsigned buttons=0,lx=0,ly=0,rx=0,ry=0;
    const auto now=nowMs();
    if((input>>token>>expires>>buttons>>lx>>ly>>rx>>ry) && token==session &&
       expires>=now && expires-now<=2000 && buttons<=65535 &&
       lx<=255 && ly<=255 && rx<=255 && ry<=255)
        ps2_stubs::setPadOverrideState(uint16_t(buttons),uint8_t(lx),uint8_t(ly),uint8_t(rx),uint8_t(ry));
    else ps2_stubs::clearPadOverrideState();
}
std::string bytes(const uint8_t* p,size_t count) {
    std::ostringstream out;out << std::hex << std::setfill('0');
    for(size_t i=0;i<count;++i) out << std::setw(2) << unsigned(p[i]);
    return out.str();
}
uint32_t pid() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<uint32_t>(getpid());
#endif
}
}
PS2Inspector::PS2Inspector() {
#ifdef _WIN32
    if(const auto p=_wgetenv(L"PS2_INSPECTOR_FILE")) m_path=p;
#else
    if(const auto p=std::getenv("PS2_INSPECTOR_FILE")) m_path=p;
#endif
    if(m_path.empty()) return;
    if(const auto p=std::getenv("PS2_INSPECTOR_SESSION")) m_session=p;
    const auto setting=std::getenv("PS2_INSPECTOR_WATCHES");
    if(setting) {
        std::istringstream input(setting);std::string item;
        while(m_watches.size()<16 && std::getline(input,item,';')) {
            try {
                const auto eq=item.find('='), colon=item.find(':');
                if(eq==std::string::npos || colon==std::string::npos || colon<=eq+1) throw std::invalid_argument("watch syntax");
                const bool indirect=item[eq+1]=='*';
                const auto address=item.substr(eq+1+indirect,colon-eq-1-indirect), size=item.substr(colon+1);
                size_t used=0;const auto a=std::stoull(address,&used,0);
                if(used!=address.size() || a>=PS2_RAM_SIZE) throw std::invalid_argument("watch address");
                const auto n=std::stoull(size,&used,0);
                if(used!=size.size() || n==0 || n>256 || item.substr(0,eq).size()>64) throw std::invalid_argument("watch length");
                m_watches.push_back({item.substr(0,eq),static_cast<uint32_t>(a),static_cast<uint32_t>(n),indirect});
            } catch(const std::exception&) {std::cerr << "[inspector] invalid watch ignored" << std::endl;}
        }
    }
    std::cerr << "[inspector] enabled; read-only EE snapshots, 60 CPU / 256 RPC history" << std::endl;
}
bool PS2Inspector::due(bool eventBoundary) {
    if(m_path.empty()) return false;
    if(!eventBoundary && m_gate++%1024!=0) return false;
    return std::chrono::steady_clock::now()>=m_next;
}
void PS2Inspector::capture(PS2Runtime& rt,const R5900Context& ctx,const uint8_t* ram,const char* state) {
    if(m_path.empty()) return;
    m_next=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
    try {
        readDiagnosticPad(m_session);
        const auto captured=nowMs();++m_sequence;
        if(m_sequence==1)rt.gs().setDebugHistoryPaused(false);
        const auto kernel=rt.eeScheduler().snapshot();
        std::vector<SifRpcDebugEvent> rpc;
        {std::lock_guard lock(g_rpc_mutex);for(const auto& event:g_sif_rpc_debug_history) if(event.seq) rpc.push_back(event);}
        std::sort(rpc.begin(),rpc.end(),[](const auto& a,const auto& b){return a.seq<b.seq;});
        std::ostringstream cpu;
        cpu << "{\"pc\":" << ctx.pc << ",\"ra\":" << getRegU32(&ctx,31) << ",\"sp\":" << getRegU32(&ctx,29)
            << ",\"gp\":" << getRegU32(&ctx,28) << ",\"gpr_u64\":[";
        for(unsigned i=0;i<32;++i) {if(i)cpu << ',';uint64_t value;std::memcpy(&value,&ctx.r[i],8);cpu << value;}
        cpu << "]}";
        std::ostringstream hist;hist << "{\"sequence\":" << m_sequence << ",\"captured_unix_ms\":" << captured
            << ",\"pc\":" << ctx.pc << ",\"ra\":" << getRegU32(&ctx,31) << '}';
        m_history.push_back(hist.str());if(m_history.size()>60)m_history.pop_front();
        std::ostringstream out;
        out << "{\"schema_version\":1,\"pid\":" << pid() << ",\"session\":" << quote(m_session)
            << ",\"ffmpeg_compiled\":" << (PS2X_HAS_FFMPEG?"true":"false")
            << ",\"sequence\":" << m_sequence << ",\"captured_unix_ms\":" << captured
            << ",\"state\":" << quote(state) << ",\"consistency\":\"EE boundary; subsystems sampled separately\",\"cpu\":" << cpu.str()
            << ",\"scheduler_sequence\":" << kernel.sequence << ",\"ee_cycle\":" << kernel.eeCycle
            << ",\"running_thread\":" << kernel.runningThreadId;
        // Captured at an EE boundary; VU execution is synchronous with EE here.
        const auto& vu = rt.vu1().state();
        uint32_t vf0[4];std::memcpy(vf0,&ctx.vu0_vf[0],sizeof(vf0));
        out << ",\"vu\":{\"vpu_stat\":" << ctx.vu0_vpu_stat
            << ",\"arithmetic_status\":" << ctx.vu0_status
            << ",\"vu0_mac_flags\":" << ctx.vu0_mac_flags
            << ",\"vu0_vf0_bits\":[" << vf0[0] << ',' << vf0[1] << ',' << vf0[2] << ',' << vf0[3] << ']'
            << ",\"vu1_pc\":" << vu.pc << ",\"vu1_cycles\":" << vu.cycles
            << ",\"vu1_top\":" << vu.top << ",\"vu1_itop\":" << vu.itop
            << ",\"vu1_ebit\":" << (vu.ebit?"true":"false")
            << ",\"vu1_halt_after_delay\":" << (vu.haltAfterDelaySlot?"true":"false")
            << ",\"vu1_stopped_d\":" << (vu.stoppedByD?"true":"false")
            << ",\"vu1_stopped_t\":" << (vu.stoppedByT?"true":"false") << '}';
        out << ",\"threads\":[";
        bool comma=false;
        for(const auto& t:kernel.threads) {
            if(comma)out << ',';comma=true;
            out << "{\"id\":" << t.id << ",\"pc\":" << t.pc << ",\"status\":" << int(t.status)
                << ",\"wait_reason\":" << int(t.waitReason) << ",\"wait_id\":" << t.waitId
                << ",\"stack\":" << t.stack << ",\"stack_size\":" << t.stackSize << '}';
        }
        out << "],\"heap\":{\"base\":" << rt.guestHeapBase() << ",\"end\":" << rt.guestHeapEnd() << ",\"limit\":" << rt.guestHeapLimit() << "},\"watches\":[";
        comma=false;
        for(const auto& w:m_watches) {
            if(comma)out << ',';comma=true;
            uint32_t address=w.address;bool valid=true;
            if(w.indirect) {if(address>PS2_RAM_SIZE-4)valid=false;else std::memcpy(&address,ram+address,4);if(!address)valid=false;}
            valid=valid && address<PS2_RAM_SIZE && w.size<=PS2_RAM_SIZE-address;
            out << "{\"name\":" << quote(w.name) << ",\"address\":" << address << ",\"valid\":" << (valid?"true":"false")
                << ",\"bytes\":" << quote(valid?bytes(ram+address,w.size):"") << '}';
        }
        out << "],\"cpu_history\":[";comma=false;
        for(const auto& entry:m_history){if(comma)out << ',';comma=true;out << entry;}
        out << "],\"rpc_capacity\":256,\"rpc_history_overwritten\":" << (rpc.empty()?0:rpc.front().seq-1) << ",\"rpc\":[";comma=false;
        for(const auto& e:rpc) {
            if(comma)out << ',';comma=true;
            out << "{\"seq\":" << e.seq << ",\"op\":" << quote(e.op?e.op:"") << ",\"sid\":" << e.sid << ",\"rpc\":" << e.rpcNum
                << ",\"pc\":" << e.pc << ",\"ra\":" << e.ra << ",\"thread\":" << e.threadId << ",\"flags\":" << e.flags
                << ",\"send_address\":" << e.sendBuf << ",\"send_size\":" << e.sendSize << ",\"recv_address\":" << e.recvBuf
                << ",\"recv_size\":" << e.recvSize << ",\"result\":" << e.result
                << ",\"send_preview\":" << quote(bytes(e.sendPreview,std::min(e.sendPreviewSize,16u)))
                << ",\"recv_preview\":" << quote(bytes(e.recvPreview,std::min(e.recvPreviewSize,16u))) << '}';
        }
        const auto iop=rt.iopDebugSnapshot();out << "],\"iop_profile\":" << quote(iop.activeProfile) << ",\"services\":[";comma=false;
        for(const auto& s:iop.services){if(comma)out << ',';comma=true;out << "{\"name\":" << quote(s.name) << ",\"metrics\":{";bool first=true;
            for(const auto& m:s.metrics){if(!first)out << ',';first=false;out << quote(m.name) << ':' << m.value;}out << "}}";}
        const auto movie=ps2_stubs::getMpegDebugSnapshot();
        out << "],\"mpeg\":{\"initialized\":" << (movie.initialized?"true":"false")
            << ",\"players\":" << movie.players << ",\"decoders\":" << movie.decoders
            << ",\"queued_pictures\":" << movie.queuedPictures << ",\"pictures_served\":" << movie.picturesServed
            << ",\"players_with_input\":" << movie.playersWithInput << ",\"waiting_for_sequence\":" << movie.waitingForSequence
            << ",\"feed_calls\":" << movie.feedCalls << ",\"cd_bytes_produced\":" << movie.cdBytesProduced
            << ",\"cd_bytes_demuxed\":" << movie.cdBytesDemuxed << '}';
        const auto graphics=rt.gs().getDebugSnapshot();
        const auto& display=rt.memory().gs();
        out << ",\"graphics\":{\"pmode\":" << display.pmode << ",\"smode2\":" << display.smode2
            << ",\"dispfb1\":" << display.dispfb1 << ",\"display1\":" << display.display1
            << ",\"dispfb2\":" << display.dispfb2 << ",\"display2\":" << display.display2
            << ",\"has_host_frame\":" << (graphics.hasHostPresentationFrame?"true":"false")
            << ",\"display_fbp\":" << graphics.hostPresentationDisplayFbp
            << ",\"source_fbp\":" << graphics.hostPresentationSourceFbp
            << ",\"used_preferred_source\":" << (graphics.hostPresentationUsedPreferred?"true":"false")
            << ",\"width\":" << graphics.hostPresentationWidth << ",\"height\":" << graphics.hostPresentationHeight
            << ",\"transfer_pixels\":" << graphics.transferTotalPixels << ",\"copied_pixels\":" << graphics.transferCopiedPixels
            << ",\"dma_starts\":" << rt.memory().dmaStartCount() << ",\"gif_copies\":" << rt.memory().gifCopyCount()
            << ",\"contexts\":[";
        for(unsigned i=0;i<2;++i){if(i)out<<',';const auto& f=graphics.ctx[i].frame;
            out << "{\"fbp\":" << f.fbp << ",\"fbw\":" << f.fbw << ",\"psm\":" << unsigned(f.psm) << '}';}
        out << "],\"recent_events\":[";comma=false;
        const auto gsHistory=rt.gs().getDebugHistory();
        const auto begin=gsHistory.size()>24?gsHistory.size()-24:0;
        for(size_t i=begin;i<gsHistory.size();++i){const auto& e=gsHistory[i];if(comma)out<<',';comma=true;
            out << "{\"seq\":" << e.seq << ",\"kind\":" << unsigned(e.kind) << ",\"register\":" << unsigned(e.reg)
                << ",\"value\":" << e.regValue << ",\"vertices\":" << e.vertexCount << ",\"fbp\":" << e.frame.fbp
                << ",\"fbw\":" << e.frame.fbw << ",\"psm\":" << unsigned(e.frame.psm)
                << ",\"gif_bytes\":" << e.gifSizeBytes << ",\"gif_nloop\":" << e.gifNloop
                << ",\"gif_flg\":" << unsigned(e.gifFlg) << ",\"gif_nreg\":" << unsigned(e.gifNreg) << '}';}
        out << "]},\"pads\":[";
        const auto pads=ps2_stubs::getPadDebugSnapshot();
        for(size_t i=0;i<ps2_stubs::kPadDebugPortCount;++i){if(i)out<<',';const auto& p=pads.ports[i][0];
            out << "{\"port\":" << i << ",\"open\":" << (p.open?"true":"false") << ",\"reads\":" << p.readCount
                << ",\"buttons_active_low\":" << p.lastButtons << ",\"used_override\":" << (p.lastUsedOverride?"true":"false")
                << ",\"lx\":" << unsigned(p.lx) << ",\"ly\":" << unsigned(p.ly)
                << ",\"rx\":" << unsigned(p.rx) << ",\"ry\":" << unsigned(p.ry)
                << ",\"last_read_ok\":" << (p.lastReadOk?"true":"false") << '}';}
        out << "],\"written_unix_ms\":" << nowMs() << '}';
        auto temp=m_path;temp += ".tmp";
        {std::ofstream file(temp,std::ios::binary|std::ios::trunc);file.exceptions(std::ios::failbit|std::ios::badbit);file << out.str();file.close();}
#ifdef _WIN32
        if(!MoveFileExW(temp.c_str(),m_path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot replace inspector file");
#else
        std::filesystem::rename(temp,m_path);
#endif
    } catch(const std::exception& e) {
        if(!m_errorLogged){std::cerr << "[inspector] " << e.what() << std::endl;m_errorLogged=true;}
    }
}
