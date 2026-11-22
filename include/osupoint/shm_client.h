#pragma once

#include "osupoint/ipc_protocol.h"
#include <chrono>

namespace osupoint {

class ShmClient {
public:
    ShmClient() noexcept = default;
    ~ShmClient() noexcept;

    ShmClient(const ShmClient&) = delete;
    ShmClient& operator=(const ShmClient&) = delete;

    bool Connect() noexcept;
    void Disconnect() noexcept;
    bool IsConnected() const noexcept { return m_connected; }

    bool ReadTelemetry(DriverTelemetry& out) noexcept;
    bool ReadDiagnostics(DebugSnapshot& out) noexcept;
    uint32_t DebugEpoch() const noexcept { return m_connected ? static_cast<uint32_t>(m_shm->debug_control.load(std::memory_order_acquire) >> 1) : 0; }
    bool WriteConfig(const TransformConfig& cfg) noexcept;

    uint32_t GetDaemonPid() const noexcept { return m_daemon_pid; }
    bool CheckLiveness() noexcept;

private:
    OsuPointSharedMemory* m_shm = nullptr;
    int m_shm_fd = -1;
    uint32_t m_daemon_pid = 0;
    bool m_connected = false;
    std::chrono::steady_clock::time_point m_last_reconnect_attempt{};
    DriverTelemetry m_last_telemetry{};
    uint32_t m_request_debug_epoch = 0;
};

} // namespace osupoint
