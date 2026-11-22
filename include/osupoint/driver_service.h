#pragma once

#include "osupoint/config_manager.h"
#include "osupoint/hidraw_reader.h"
#include "osupoint/input_pipeline.h"
#include "osupoint/rt_scheduler.h"
#include "osupoint/uinput_emitter.h"
#include <mutex>
#include <thread>

namespace osupoint {

class DriverService {
public:
    static DriverService& Instance() { static DriverService instance; return instance; }
    bool Start(const std::string& config_path = "", bool mouse_mode = true,
               int spin_percent = 0, int core_reader = -1, int core_pipeline = -1,
               const std::string& device_path = "", const TransformConfig* initial_config = nullptr,
               uint16_t vendor_id = 0, uint16_t product_id = 0);
    // Isolated socket/pipe test seam: no HID, uinput, IPC, RT or memory locking.
    bool StartForTesting(int input_fd, int output_fd, const RuntimeTabletSpec& spec,
                         const TransformConfig& cfg);
    void Stop();
    bool UpdateConfig(const TransformConfig& cfg);
    void ReadTelemetry(DriverTelemetry& out) const noexcept;
    bool ReadDiagnostics(DebugSnapshot& out) const noexcept;
    uint32_t DebugEpoch() const noexcept { return static_cast<uint32_t>(m_requested_debug_control.load(std::memory_order_acquire) >> 1); }
    bool IsRunning() const noexcept { return m_running.load(std::memory_order_acquire); }
    bool IsConnected() const noexcept { return m_connected.load(std::memory_order_acquire); }
    TabletSpec GetTabletSpec() const noexcept;
private:
    friend struct DriverServiceTestAccess;
    DriverService() = default;
    ~DriverService() { Stop(); }
    DriverService(const DriverService&) = delete;
    DriverService& operator=(const DriverService&) = delete;
    void PipelineLoop();
    void Publish(uint64_t now_ns);
    void Wake() noexcept;
    void ReleaseResources() noexcept;
    ShmConfigBlock& ConfigBlock() noexcept { return m_shm ? m_shm->config_block : m_local_config; }
    std::atomic<uint64_t>& DebugControl() noexcept { return m_shm ? m_shm->debug_control : m_local_debug_control; }

    std::mutex m_lifecycle;
    std::atomic<bool> m_running{false}, m_connected{false};
    std::atomic<uint64_t> m_debug_ack{0}, m_requested_debug_control{0}, m_local_debug_control{0};
    uint32_t m_debug_epoch = 1, m_request_debug_epoch = 0;
    std::string m_config_path, m_device_path, m_shm_path;
    uint16_t m_filter_vid = 0, m_filter_pid = 0;
    int m_core_pipeline = -1, m_shm_fd = -1, m_lock_fd = -1, m_wake_fd = -1;
    bool m_owns_shm = false, m_testing = false;
    int m_test_input_fd = -1, m_test_output_fd = -1;
    ShmConfigBlock m_local_config;
    ShmTelemetryBlock m_local_telemetry;
    AtomicSnapshot<DebugSnapshot> m_local_debug;
    AtomicSnapshot<RuntimeTabletSpec> m_spec_snapshot;
    OsuPointSharedMemory* m_shm = nullptr;

    // Only PipelineLoop owns all device handles, filter and debug state.
    DiscoveredTablet m_tablet{};
    HidrawReader m_reader;
    UinputEmitter m_emitter;
    InputPipeline m_pipeline;
    DebugSession m_debug;
    DriverTelemetry m_telemetry{};
    bool m_published_debug = false;
    uint64_t m_continuous_reports = 0;
    uint64_t m_last_hz_calc_ns = 0;
    uint64_t m_reports_at_last_calc = 0;
    double m_continuous_hz = 0.0;
    double m_ema_latency_us = 0.0;
    double m_session_max_latency_us = 0.0;
    double m_ema_jitter_us = 0.0;
    double m_last_interval_us = 0.0;
    uint64_t m_last_packet_arrival_ns = 0;
    std::thread m_pipeline_thread;
};

} // namespace osupoint
