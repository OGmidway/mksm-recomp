#include "module_factories.h"
#include "mk_sndf_control.h"

#include <array>
#include <cstring>
#include <limits>
#include <mutex>
#include <sstream>

namespace ps2x::iop::detail
{
    namespace
    {
        // SLUS_210.87: producer 0x463BD8, completion callback 0x463880.
        // sndfi.irx: SFSV registration at 0xBAA4, handler at 0xC2C8
        // (module-relative addresses). See games/mk-shaolin-monks/SFSV-RPC.md.
        class MkSfsvService final : public IopService
        {
        public:
            explicit MkSfsvService(IopHost &host) : m_host(host), m_sndf(host) {}
            std::string_view name() const override { return "Shaolin Monks SFSV update transport"; }
            std::span<const uint32_t> sids() const override { return m_sids; }
            void reset() override
            {
                std::lock_guard lock(m_mutex);
                m_sndf.reset();
                m_idle = m_busy = m_controls = m_accepted = 0;
                m_busyHashes.clear();
            }
            RpcResult handleRpc(const RpcRequest &request) override
            {
                if (request.sid == m_sids[1])
                {
                    std::lock_guard lock(m_mutex);
                    return m_sndf.handle(request);
                }
                constexpr uint32_t sendBytes = 0xA40, receiveBytes = 0x1C0;
                if (request.sid != m_sids[0] || request.function != 0x8000 ||
                    request.send.size != sendBytes || request.receive.size != receiveBytes ||
                    request.send.address == 0 || request.receive.address == 0 ||
                    request.send.address > std::numeric_limits<uint32_t>::max() - sendBytes ||
                    request.receive.address > std::numeric_limits<uint32_t>::max() - receiveBytes)
                    return {};

                std::array<uint8_t, sendBytes> packet{};
                if (!m_host.readGuest(request.send.address, packet.data(), packet.size()))
                    return {};
                // The count is AFTER 128 twenty-byte command records, not at byte zero.
                const uint32_t count = uint32_t(packet[0xA00]) |
                    (uint32_t(packet[0xA01]) << 8) | (uint32_t(packet[0xA02]) << 16) |
                    (uint32_t(packet[0xA03]) << 24);
                if (count > 128) return {};

                std::lock_guard lock(m_mutex);
                bool supported = false;
                if (!m_sndf.updateSound(packet, count, request.receive.address, supported))
                    return {};
                if (supported && count != 0)
                {
                    ++m_accepted;
                    for (uint32_t i = 0; i < count; ++i)
                        m_controls += packet[i * 20] == 1 && packet[i * 20 + 1] == 0;
                }
                else if (count == 0)
                {
                    if (++m_idle == 1)
                        m_host.log(LogLevel::Info, "[MK SFSV] idle update accepted");
                }
                else
                {
                    ++m_busy;
                    uint64_t hash = 14695981039346656037ull;
                    for (unsigned i = 0; i < count * 20; ++i)
                        hash = (hash ^ packet[i]) * 1099511628211ull;
                    if (m_busyHashes.size() < 8 &&
                        std::find(m_busyHashes.begin(), m_busyHashes.end(), hash) == m_busyHashes.end())
                    {
                        m_busyHashes.push_back(hash);
                        std::ostringstream message;
                        message << "[MK SFSV] unsupported or unavailable sound batch; busy=1 count=" << count;
                        for (uint32_t i = 0; i < count && i < 8; ++i)
                        {
                            message << " record[" << i << "]=" << std::hex;
                            for (uint32_t j = 0; j < 20; ++j)
                                message << unsigned(packet[i * 20 + j]) << ',';
                        }
                        m_host.log(LogLevel::Warning, message.str());
                    }
                }
                RpcResult result;
                result.handled = true;
                result.resultAddress = request.receive.address;
                result.signalNowaitCompletion = true;
                // Preserve the real EE callback; it consumes the receive packet.
                result.serverDispatchPolicy = ServerDispatchPolicy::Suppress;
                return result;
            }
            void appendDebugMetrics(std::vector<DebugMetric> &metrics) const override
            {
                std::lock_guard lock(m_mutex);
                m_sndf.metrics(metrics);
                metrics.push_back({"idle_updates", m_idle, false});
                metrics.push_back({"audio_batches_busy", m_busy, false});
                metrics.push_back({"audio_batches_accepted", m_accepted, false});
                metrics.push_back({"output_mode", m_sndf.outputMode(), false});
                metrics.push_back({"output_mode_commands", m_controls, false});
            }
        private:
            static uint32_t readLe32(const uint8_t *p)
            {
                return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
                       (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
            }
            IopHost &m_host;
            MkSndfControl m_sndf;
            const std::array<uint32_t, 2> m_sids{0x53465356u, 0x534e4446u};
            mutable std::mutex m_mutex;
            uint64_t m_idle = 0, m_busy = 0, m_controls = 0, m_accepted = 0;
            std::vector<uint64_t> m_busyHashes;
        };
    }
    std::unique_ptr<IopService> createMkSfsvService(IopHost &host)
    {
        return std::make_unique<MkSfsvService>(host);
    }
}
