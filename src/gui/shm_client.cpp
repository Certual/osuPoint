#include "osupoint/shm_client.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <cerrno>

namespace osupoint {

ShmClient::~ShmClient() noexcept {
    Disconnect();
}

bool ShmClient::Connect() noexcept {
    if (m_connected && CheckLiveness()) {
        return true;
    }

    auto now = std::chrono::steady_clock::now();
    if (m_last_reconnect_attempt.time_since_epoch().count() > 0) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_reconnect_attempt);
        if (elapsed.count() < 1000) {
            return false;
        }
    }
    m_last_reconnect_attempt = now;

    Disconnect();

    char shm_path[64];
    GetIpcShmPath(shm_path, sizeof(shm_path));

    m_shm_fd = shm_open(shm_path, O_RDWR, 0660);
    if (m_shm_fd < 0) {
        return false;
    }

    void* ptr = mmap(nullptr, sizeof(OsuPointSharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED, m_shm_fd, 0);
    if (ptr == MAP_FAILED) {
        close(m_shm_fd);
        m_shm_fd = -1;
        return false;
    }

    m_shm = static_cast<OsuPointSharedMemory*>(ptr);
    if (m_shm->magic != OSUPOINT_IPC_MAGIC || m_shm->version != OSUPOINT_IPC_VERSION) {
        munmap(m_shm, sizeof(OsuPointSharedMemory));
        m_shm = nullptr;
        close(m_shm_fd);
        m_shm_fd = -1;
        return false;
    }

    m_daemon_pid = m_shm->daemon_pid;
    if (m_daemon_pid == 0 || kill(static_cast<pid_t>(m_daemon_pid), 0) != 0) {
        Disconnect();
        shm_unlink(shm_path);
        return false;
    }

    m_shm->gui_pid = static_cast<uint32_t>(getpid());
    m_connected = true;

    return true;
}

void ShmClient::Disconnect() noexcept {
    if (m_shm != nullptr && m_shm != MAP_FAILED) {
        m_shm->gui_pid = 0;
        munmap(m_shm, sizeof(OsuPointSharedMemory));
        m_shm = nullptr;
    }
    if (m_shm_fd >= 0) {
        close(m_shm_fd);
        m_shm_fd = -1;
    }
    m_connected = false;
    m_daemon_pid = 0;
}

bool ShmClient::CheckLiveness() noexcept {
    if (!m_connected || m_shm == nullptr) {
        return false;
    }

    if (m_daemon_pid == 0) {
        Disconnect();
        return false;
    }

    if (kill(static_cast<pid_t>(m_daemon_pid), 0) != 0) {
        if (errno == ESRCH) {
            Disconnect();
            return false;
        }
    }

    return true;
}

bool ShmClient::ReadTelemetry(DriverTelemetry& out) noexcept {
    if (!m_connected) {
        Connect();
    }

    if (!m_connected || !CheckLiveness()) {
        out = m_last_telemetry;
        out.connected = 0;
        return false;
    }

    DriverTelemetry read_val;
    if (::ReadTelemetry(m_shm->telemetry_block, read_val, 10)) {
        m_last_telemetry = read_val;
        out = read_val;
        return true;
    }

    out = m_last_telemetry;
    return false;
}

bool ShmClient::ReadDiagnostics(DebugSnapshot& out) noexcept {
    if (!m_connected) {
        Connect();
    }

    if (!m_connected || !CheckLiveness()) {
        out = {};
        return false;
    }

    return m_shm->debug_block.Read(out);
}

bool ShmClient::WriteConfig(const TransformConfig& cfg) noexcept {
    if (!m_connected) {
        Connect();
    }

    if (!m_connected || !CheckLiveness()) {
        return false;
    }

    ::WriteConfig(m_shm->config_block, cfg);
    return true;
}

} // namespace osupoint
