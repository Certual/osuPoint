#include "osupoint/driver_service.h"
#include "osupoint/tablet_decoder.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <poll.h>
#include <signal.h>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace osupoint {

bool DriverService::Start(const std::string& path, bool mouse_mode, int spin,
                          int /*core_reader*/, int core, const std::string& device, const TransformConfig* initial,
                          uint16_t vendor_id, uint16_t product_id) {
    std::lock_guard guard(m_lifecycle);
    if (IsRunning()) return true;
    m_testing = false;
    m_core_pipeline = core;
    m_config_path = path;
    m_device_path = device;
    m_filter_vid = vendor_id; m_filter_pid = product_id;
    char name[80];
    std::snprintf(name, sizeof(name), "/dev/shm/osupoint_driver_%u.lock", getuid());
    m_lock_fd = open(name, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (m_lock_fd < 0) return false;
    if (flock(m_lock_fd, LOCK_EX | LOCK_NB) != 0) { ReleaseResources(); errno = EBUSY; return false; }
    GetIpcShmPath(name, sizeof(name));
    m_shm_path = name;
    // Do not truncate the IPC of a running older build which doesn't use flock.
    int previous = shm_open(name, O_RDONLY | O_CLOEXEC, 0);
    if (previous >= 0) {
        struct Header { uint32_t magic, version, size, pid; } header{};
        const ssize_t count = pread(previous, &header, sizeof(header), 0);
        close(previous);
        if (count == sizeof(header) && header.magic == OSUPOINT_IPC_MAGIC && header.pid != 0 &&
            (kill(static_cast<pid_t>(header.pid), 0) == 0 || errno == EPERM)) {
            ReleaseResources(); errno = EBUSY; return false;
        }
    }
    m_shm_fd = shm_open(name, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (m_shm_fd < 0 || ftruncate(m_shm_fd, sizeof(OsuPointSharedMemory)) != 0) { ReleaseResources(); return false; }
    void* mapping = mmap(nullptr, sizeof(OsuPointSharedMemory), PROT_READ | PROT_WRITE, MAP_SHARED, m_shm_fd, 0);
    if (mapping == MAP_FAILED) { ReleaseResources(); return false; }
    m_shm = new (mapping) OsuPointSharedMemory{};
    m_owns_shm = true;
    m_shm->daemon_pid = static_cast<uint32_t>(getpid());
    m_wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_wake_fd < 0) { ReleaseResources(); return false; }

    OsuPointConfig file;
    ConfigManager::LoadFromFile(path, file);
    TransformConfig cfg{};
    ConfigManager::ToTransformConfig(file, cfg);
    if (initial) cfg = *initial;
    cfg.mouse_mode = mouse_mode ? 1 : 0;
    if (spin > 0) cfg.spin_percent = std::clamp(spin, 0, 100);
    if (cfg.mx[0] == 0 && cfg.my[0] == 0) {
        ConfigManager::FromTransformConfig(cfg, file);
        file.ToTransformConfig(RuntimeTabletSpec{}, cfg);
    }
    if (!ValidTransformConfig(cfg)) { ReleaseResources(); errno = EINVAL; return false; }
    cfg.debug_epoch = m_debug_epoch = 1;
    m_request_debug_epoch = 0;
    const uint64_t debug_control = 2 | uint64_t(bool(cfg.debug_mode));
    DebugControl().store(debug_control, std::memory_order_release);
    m_requested_debug_control.store(debug_control, std::memory_order_release);
    WriteConfig(ConfigBlock(), cfg);
    m_debug_ack.store(0, std::memory_order_relaxed);
    m_spec_snapshot.Write(RuntimeTabletSpec{});
    m_running.store(true, std::memory_order_release);
    m_pipeline_thread = std::thread(&DriverService::PipelineLoop, this);
    m_shm->magic.store(OSUPOINT_IPC_MAGIC, std::memory_order_release);
    return true;
}

bool DriverService::StartForTesting(int input, int output, const RuntimeTabletSpec& spec,
                                   const TransformConfig& cfg) {
    std::lock_guard guard(m_lifecycle);
    if (IsRunning() || !ValidTransformConfig(cfg)) return false;
    m_test_input_fd = fcntl(input, F_DUPFD_CLOEXEC, 0);
    m_test_output_fd = fcntl(output, F_DUPFD_CLOEXEC, 0);
    m_wake_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_test_input_fd < 0 || m_test_output_fd < 0 || m_wake_fd < 0) { ReleaseResources(); return false; }
    m_testing = true;
    m_core_pipeline = -1;
    m_tablet = {};
    m_tablet.spec = spec;
    m_spec_snapshot.Write(spec);
    TransformConfig initial = cfg;
    initial.debug_epoch = m_debug_epoch = 1;
    m_request_debug_epoch = cfg.debug_epoch;
    const uint64_t debug_control = 2 | uint64_t(bool(cfg.debug_mode));
    DebugControl().store(debug_control, std::memory_order_release);
    m_requested_debug_control.store(debug_control, std::memory_order_release);
    WriteConfig(m_local_config, initial);
    m_debug_ack.store(0, std::memory_order_relaxed);
    m_running.store(true, std::memory_order_release);
    m_pipeline_thread = std::thread(&DriverService::PipelineLoop, this);
    return true;
}

void DriverService::Wake() noexcept {
    if (m_wake_fd >= 0) { uint64_t one = 1; (void)write(m_wake_fd, &one, sizeof(one)); }
}

void DriverService::ReleaseResources() noexcept {
    if (m_shm) { munmap(m_shm, sizeof(OsuPointSharedMemory)); m_shm = nullptr; }
    if (m_shm_fd >= 0) { close(m_shm_fd); m_shm_fd = -1; }
    if (m_owns_shm) { shm_unlink(m_shm_path.c_str()); m_owns_shm = false; }
    for (int* fd : {&m_wake_fd, &m_test_input_fd, &m_test_output_fd, &m_lock_fd})
        if (*fd >= 0) { close(*fd); *fd = -1; }
}

void DriverService::Stop() {
    std::lock_guard guard(m_lifecycle);
    m_running.store(false, std::memory_order_release);
    Wake();
    if (m_pipeline_thread.joinable()) m_pipeline_thread.join();
    ReleaseResources();
}

bool DriverService::UpdateConfig(const TransformConfig& requested) {
    std::lock_guard guard(m_lifecycle);
    if (!IsRunning() || !ValidTransformConfig(requested)) { errno = EINVAL; return false; }
    TransformConfig cfg = requested;
    uint64_t token = DebugControl().load(std::memory_order_acquire);
    const bool was_enabled = token & 1;
    if (was_enabled != bool(cfg.debug_mode) || requested.debug_epoch != m_request_debug_epoch)
        token = SetDebugCommand(DebugControl(), cfg.debug_mode);
    m_debug_epoch = static_cast<uint32_t>(token >> 1);
    m_request_debug_epoch = requested.debug_epoch;
    cfg.debug_epoch = m_debug_epoch;
    ObserveDebugCommand(m_requested_debug_control, token);
    const uint64_t written = WriteConfig(ConfigBlock(), cfg);
    Wake();
    if (was_enabled && !cfg.debug_mode) {
        // Control-plane barrier: OFF returns only after the owner has erased its
        // buffers and published the empty snapshot, not merely hidden the panel.
        const uint64_t deadline = GetMonotonicTimeNs() + 500000000ULL;
        while (m_debug_ack.load(std::memory_order_acquire) != token) {
            if (DebugControl().load(std::memory_order_acquire) != token) { errno = ECANCELED; return false; }
            if (GetMonotonicTimeNs() >= deadline) { errno = ETIMEDOUT; return false; }
            usleep(1000);
        }
    }
    if (written == OSUPOINT_WRITE_FAILED) { errno = EBUSY; return false; }
    return true;
}

void DriverService::ReadTelemetry(DriverTelemetry& out) const noexcept {
    ::ReadTelemetry(m_local_telemetry, out);
}

bool DriverService::ReadDiagnostics(DebugSnapshot& out) const noexcept {
    const uint64_t requested = m_requested_debug_control.load(std::memory_order_acquire);
    if (!IsRunning() || !(requested & 1) || m_debug_ack.load(std::memory_order_acquire) != requested) { out = {}; return false; }
    return m_local_debug.Read(out);
}

TabletSpec DriverService::GetTabletSpec() const noexcept {
    TabletSpec spec{};
    m_spec_snapshot.Read(spec);
    return spec;
}

void DriverService::Publish(uint64_t now_ns) {
    if (m_debug.Enabled()) {
        const DebugSnapshot d = m_debug.Snapshot(now_ns);
        m_local_debug.Write(d);
        if (m_shm) m_shm->debug_block.Write(d);
        m_published_debug = true;
        m_telemetry.debug_enabled = 1;
        m_telemetry.total_reports = d.received;
        m_telemetry.dropped_reports = d.write_errors;
        m_telemetry.current_hz = d.input_hz > 0.0 ? d.input_hz : m_continuous_hz;
        m_telemetry.latency_p50_us = d.latency_p50_us;
        m_telemetry.latency_p99_us = d.latency_p99_us;
        m_telemetry.latency_max_us = d.latency_session_max_us;
        m_telemetry.jitter_us = d.jitter_us;
        m_telemetry.jitter_rfc_us = d.jitter_rfc_us;
        m_telemetry.interval_stddev_us = d.interval_stddev_us;
    } else {
        if (m_published_debug) {
            m_local_debug.Write(DebugSnapshot{});
            if (m_shm) m_shm->debug_block.Write(DebugSnapshot{});
            m_published_debug = false;
        }
        m_telemetry.debug_enabled = 0;
        m_telemetry.total_reports = m_continuous_reports;
        m_telemetry.dropped_reports = 0;
        m_telemetry.current_hz = m_continuous_hz;
        m_telemetry.latency_p50_us = m_ema_latency_us;
        m_telemetry.latency_p99_us = m_ema_latency_us * 1.5;
        m_telemetry.latency_max_us = m_session_max_latency_us;
        m_telemetry.jitter_us = m_ema_jitter_us;
        m_telemetry.jitter_rfc_us = m_ema_jitter_us;
        m_telemetry.interval_stddev_us = m_ema_jitter_us * 0.5;
    }
    WriteTelemetry(m_local_telemetry, m_telemetry);
    if (m_shm) WriteTelemetry(m_shm->telemetry_block, m_telemetry);
}

void DriverService::PipelineLoop() {
    m_telemetry = {};
    m_telemetry.started_ns = GetMonotonicTimeNs();
    m_telemetry.pipeline_tid = static_cast<int32_t>(syscall(SYS_gettid));
    m_telemetry.requested_core = m_core_pipeline;
    if (!m_testing) {
        m_telemetry.affinity_ok = m_core_pipeline < 0 || SetThreadAffinity(m_core_pipeline);
        SetRealtimePriority(50);
        m_telemetry.memory_locked = LockProcessMemory();
    }
    struct sched_param priority{};
    pthread_getschedparam(pthread_self(), &m_telemetry.scheduler_policy, &priority);
    m_telemetry.scheduler_priority = priority.sched_priority;
    m_telemetry.nice_value = getpriority(PRIO_PROCESS, 0);
    m_pipeline.Reset();
    m_debug.SetEnabled(false);
    m_continuous_reports = 0;
    m_last_hz_calc_ns = 0;
    m_reports_at_last_calc = 0;
    m_continuous_hz = 0.0;
    m_ema_latency_us = 0.0;
    m_session_max_latency_us = 0.0;
    m_ema_jitter_us = 0.0;
    m_last_interval_us = 0.0;
    m_last_packet_arrival_ns = 0;
    m_ema_jitter_us = 0.0;
    m_last_interval_us = 0.0;
    m_last_packet_arrival_ns = 0;
    TransformConfig cfg{};
    uint64_t revision = UINT64_MAX, last_packet_ns = 0, last_publish = 0;
    uint64_t next_reconnect = 0, last_heartbeat = 0;
    uint64_t debug_control = UINT64_MAX;
    int input = -1;
    bool output_mode = true;
    uint16_t output_pressure_max = 0;
    int output_rx = 0, output_ry = 0;
    bool test_disconnected = false;

    const auto emit = [&](const InputOutput& out) {
        return cfg.mouse_mode ? m_emitter.EmitMouseSample(out.x, out.y, out.left, out.right)
            : m_emitter.EmitStylusSample(out.x, out.y, out.pressure, out.in_range, out.left, out.right);
    };
    while (m_running.load(std::memory_order_acquire)) {
        uint64_t now = GetMonotonicTimeNs();
        const uint64_t command = DebugControl().load(std::memory_order_acquire);
        if (command != debug_control) {
            debug_control = command;
            ObserveDebugCommand(m_requested_debug_control, command);
            m_debug.SetEnabled(command & 1, now);
            m_telemetry.debug_epoch = static_cast<uint32_t>(command >> 1);
            Publish(now);
            last_publish = now;
            m_debug_ack.store(command, std::memory_order_release);
            if (m_shm) m_shm->applied_debug_control.store(command, std::memory_order_release);
        }
        auto& block = ConfigBlock();
        const uint64_t observed = block.seq.load(std::memory_order_acquire);
        if (observed != revision && !(observed & 1)) {
            TransformConfig next{};
            if (ReadConfig(block, next, 2) && observed == block.seq.load(std::memory_order_acquire)) {
                revision = observed;
                if (ValidTransformConfig(next)) {
                    cfg = next;
                    m_tablet.spec.tip_pressure_threshold = cfg.tip_pressure_threshold;
                    m_pipeline.Configure(m_tablet.spec, cfg);
                    m_telemetry.last_error = 0;
                    if (m_shm) m_shm->applied_config_seq.store(revision, std::memory_order_release);
                } else m_telemetry.last_error = EINVAL;
            }
        }

        if (output_mode != bool(cfg.mouse_mode) || (!cfg.mouse_mode && output_pressure_max != cfg.pressure_max) ||
            (!cfg.mouse_mode && (output_rx != cfg.output_resolution_x || output_ry != cfg.output_resolution_y)) ||
            (!m_emitter.IsInitialized() && now >= next_reconnect)) {
            output_mode = cfg.mouse_mode;
            output_pressure_max = cfg.pressure_max;
            output_rx = cfg.output_resolution_x; output_ry = cfg.output_resolution_y;
            bool ok = m_testing ? m_emitter.InitializeForTesting(m_test_output_fd, output_mode)
                                : m_emitter.Initialize(output_mode, "osuPoint Virtual Tablet", cfg.pressure_max, output_rx, output_ry);
            m_telemetry.output_resolution_x = output_rx; m_telemetry.output_resolution_y = output_ry;
            m_telemetry.output_ready = ok;
            m_telemetry.last_error = ok ? 0 : errno;
            std::snprintf(m_telemetry.output_name, sizeof(m_telemetry.output_name), "%s", m_testing ? "test pipe" : m_emitter.GetSysName());
            m_pipeline.Reset();
            Publish(now);
            if (!ok) next_reconnect = now + 1000000000ULL;
        }
        if (input < 0 && !test_disconnected && now >= next_reconnect) {
            bool opened = false;
            if (m_testing) { input = m_test_input_fd; opened = true; }
            else {
                DiscoveredTablet tablet{};
                if (HidrawReader::DiscoverTablet(tablet, m_device_path.empty() ? nullptr : m_device_path.c_str(), m_filter_vid, m_filter_pid) && m_reader.Open(tablet)) {
                    m_tablet = tablet;
                    input = m_reader.GetFd();
                    opened = true;
                }
            }
            if (opened) {
                m_connected.store(true, std::memory_order_release);
                m_telemetry.connected = 1;
                m_telemetry.initialization_error = m_testing ? 0 : m_reader.GetInitializationError();
                m_telemetry.evdev_grabbed = m_testing ? 0 : m_reader.IsEvdevGrabbed();
                m_telemetry.vendor_id = m_tablet.vid ? m_tablet.vid : m_tablet.spec.vid;
                m_telemetry.product_id = m_tablet.pid ? m_tablet.pid : m_tablet.spec.pid;
                m_tablet.spec.tip_pressure_threshold = cfg.tip_pressure_threshold;
                m_telemetry.max_x = m_tablet.spec.max_x; m_telemetry.max_y = m_tablet.spec.max_y;
                m_telemetry.phys_w_mm = m_tablet.spec.phys_w; m_telemetry.phys_h_mm = m_tablet.spec.phys_h;
                m_telemetry.report_id = m_tablet.spec.report_id; m_telemetry.coord_bits = m_tablet.spec.coord_bits;
                m_telemetry.x_offset = m_tablet.spec.x_offset; m_telemetry.y_offset = m_tablet.spec.y_offset;
                std::snprintf(m_telemetry.device_name, sizeof(m_telemetry.device_name), "%s", m_tablet.spec.name);
                std::snprintf(m_telemetry.hidraw_path, sizeof(m_telemetry.hidraw_path), "%s", m_testing ? "test socket" : m_tablet.hidraw_path);
                m_spec_snapshot.Write(m_tablet.spec);
                if (cfg.mx[0] == 0 && cfg.my[0] == 0) {
                    OsuPointConfig area;
                    ConfigManager::FromTransformConfig(cfg, area);
                    area.ToTransformConfig(m_tablet.spec, DisplayGeometry{}, cfg);
                } else RebindTabletTransform(m_tablet.spec, cfg);
                m_pipeline.Reset();
                m_pipeline.Configure(m_tablet.spec, cfg);
                last_packet_ns = 0;
                m_debug.Reconnect();
                Publish(now);
            }
            next_reconnect = now + 1000000000ULL;
        }
        if (m_shm && now - last_heartbeat >= 200000000ULL) {
            m_shm->heartbeat_timestamp_ns.store(now, std::memory_order_release);
            last_heartbeat = now;
        }
        if (now - m_last_hz_calc_ns >= 200000000ULL) {
            if (m_last_hz_calc_ns > 0) {
                const double dt = (now - m_last_hz_calc_ns) * 1e-9;
                const uint64_t diff = m_continuous_reports >= m_reports_at_last_calc ? (m_continuous_reports - m_reports_at_last_calc) : 0;
                const double instant_hz = dt > 0.0 ? (static_cast<double>(diff) / dt) : 0.0;
                m_continuous_hz = (m_continuous_hz == 0.0) ? instant_hz : (m_continuous_hz * 0.3 + instant_hz * 0.7);
            }
            m_last_hz_calc_ns = now;
            m_reports_at_last_calc = m_continuous_reports;
        }
        if (now - last_publish >= 100000000ULL) {
            Publish(now);
            last_publish = now;
        }

        struct pollfd fds[2] = {{m_wake_fd, POLLIN, 0}, {input, POLLIN, 0}};
        int ready = poll(fds, 2, 100);
        if (ready < 0) { if (errno != EINTR) m_debug.ReadError(errno); continue; }
        if (fds[0].revents & POLLIN) {
            uint64_t count;
            (void)read(m_wake_fd, &count, sizeof(count));
            continue; // Apply control changes before another input batch.
        }
        if (input < 0 || !(fds[1].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL))) continue;

        uint8_t staged_buffer[64];
        ssize_t staged_size = 0;

        while (m_running.load(std::memory_order_relaxed)) {
            bool disconnected = false;
            InputOutput latest_out{};
            DebugPoint latest_point{};
            PenSample latest_sample{};
            uint64_t latest_begin = 0;
            bool has_latest = false;
            bool saw_tap_down = false;
            bool saw_barrel_down = false;

            // Synchronous Drain-to-Latest: drain all pending kernel hidraw reports
            // to prevent queue accumulation and lag drift while preserving button clicks.
            for (unsigned burst = 0; burst < 64 && m_running.load(std::memory_order_relaxed); ++burst) {
                const uint64_t pending = block.seq.load(std::memory_order_acquire);
                if (pending != revision && !(pending & 1)) break;
                uint8_t buffer[64];
                ssize_t size = 0;
                if (staged_size > 0) {
                    std::memcpy(buffer, staged_buffer, static_cast<size_t>(staged_size));
                    size = staged_size;
                    staged_size = 0;
                } else {
                    size = m_testing ? recv(input, buffer, sizeof(buffer), MSG_DONTWAIT)
                                     : read(input, buffer, sizeof(buffer));
                }
                if (size <= 0) {
                    if (size < 0 && errno == EINTR) continue;
                    if (size < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        break;
                    }
                    m_debug.ReadError(size < 0 ? errno : ENODEV);
                    disconnected = true;
                    break;
                }
                const uint64_t begin = GetMonotonicTimeNs();
                if (m_last_packet_arrival_ns > 0 && begin > m_last_packet_arrival_ns) {
                    const uint64_t intv_ns = begin - m_last_packet_arrival_ns;
                    if (intv_ns < 100000000ULL) {
                        const double intv_us = intv_ns * 0.001;
                        if (m_last_interval_us > 0.0) {
                            const double diff = std::abs(intv_us - m_last_interval_us);
                            m_ema_jitter_us = (m_ema_jitter_us == 0.0) ? diff : (m_ema_jitter_us * 0.9 + diff * 0.1);
                        }
                        m_last_interval_us = intv_us;
                    }
                }
                m_last_packet_arrival_ns = begin;
                const bool debug = m_debug.Enabled();
                if (debug) m_debug.Receive(begin, buffer, static_cast<size_t>(size));
                PenSample sample{};
                const DecodeReason reason = DecodeTabletPacket(buffer, static_cast<size_t>(size), m_tablet.spec, sample);
                if (debug) m_debug.Decode(static_cast<unsigned>(reason));
                InputOutput out{};
                DebugPoint point{};
                bool processed = false;
                if (reason == DecodeReason::Ok) {
                    ++m_continuous_reports;
                    const double dt = last_packet_ns && begin > last_packet_ns ? (begin - last_packet_ns) * 1e-9 : 0;
                    last_packet_ns = begin;
                    processed = m_pipeline.Process(sample, dt, out, debug ? &point : nullptr);
                } else if (reason == DecodeReason::LiftSignature || reason == DecodeReason::ZeroFrame) {
                    const bool was_active = m_pipeline.Last().in_range || m_pipeline.Last().left || m_pipeline.Last().right;
                    if (was_active) {
                        out = m_pipeline.Lift();
                        last_packet_ns = 0;
                        processed = true;
                    }
                }
                if (!processed) continue;

                if (out.left) saw_tap_down = true;
                if (out.right) saw_barrel_down = true;

                if (has_latest && debug) {
                    m_debug.Output(0, false, true, 0,
                                   latest_sample.raw_x, latest_sample.raw_y,
                                   latest_out.x, latest_out.y,
                                   latest_sample.pressure, latest_sample.flags,
                                   latest_point,
                                   (latest_sample.flags & PEN_FLAG_IN_RANGE) != 0);
                }

                latest_out = out;
                latest_point = point;
                latest_sample = sample;
                latest_begin = begin;
                has_latest = true;
            }

            if (has_latest) {
                const bool debug = m_debug.Enabled();

                // Click edge synthesis: if a micro-tap (press + release) occurred completely
                // within the drained burst, synthesize the down transition first at latest coordinates.
                if (saw_tap_down && !latest_out.left && !m_pipeline.Last().left) {
                    InputOutput tap_down = latest_out;
                    tap_down.left = true;
                    if (emit(tap_down)) {
                        m_pipeline.Commit(tap_down);
                    }
                }
                if (saw_barrel_down && !latest_out.right && !m_pipeline.Last().right) {
                    InputOutput barrel_down = latest_out;
                    barrel_down.right = true;
                    if (emit(barrel_down)) {
                        m_pipeline.Commit(barrel_down);
                    }
                }

                const bool duplicate = !m_pipeline.ShouldEmit(latest_out);
                const bool emitted = !duplicate && emit(latest_out);
                const int error = emitted || duplicate ? 0 : errno;
                if (emitted) m_pipeline.Commit(latest_out);
                const uint64_t elapsed = latest_begin ? (GetMonotonicTimeNs() - latest_begin) : 0;
                const double lat_us = elapsed * 0.001;
                if (lat_us > 0.0 && lat_us < 100000.0) {
                    m_ema_latency_us = (m_ema_latency_us == 0.0) ? lat_us : (m_ema_latency_us * 0.9 + lat_us * 0.1);
                    if (lat_us > m_session_max_latency_us) m_session_max_latency_us = lat_us;
                }
                if (debug) {
                    m_debug.Output(elapsed, emitted, duplicate, error,
                                   latest_sample.raw_x, latest_sample.raw_y,
                                   latest_out.x, latest_out.y,
                                   latest_sample.pressure, latest_sample.flags,
                                   latest_point,
                                   (latest_sample.flags & PEN_FLAG_IN_RANGE) != 0);
                }
                if (!emitted && !duplicate && (error == ENODEV || error == EBADF || error == EIO)) {
                    m_emitter.Destroy(); m_telemetry.output_ready = 0; m_telemetry.last_error = error;
                }
            }

            if (disconnected) {
                const InputOutput lift = m_pipeline.Lift();
                if (emit(lift)) m_pipeline.Commit(lift);
                if (!m_testing) m_reader.Close(); else test_disconnected = true;
                input = -1;
                m_connected.store(false, std::memory_order_release);
                m_telemetry.connected = 0; m_telemetry.last_error = ENODEV;
                m_telemetry.evdev_grabbed = 0;
                last_packet_ns = 0; next_reconnect = GetMonotonicTimeNs() + 1000000000ULL;
                Publish(GetMonotonicTimeNs());
                break;
            }

            // Periodic in-burst telemetry update and publishing
            const uint64_t now_burst = GetMonotonicTimeNs();

            // Check debug control updates while in continuous motion
            const uint64_t command_burst = DebugControl().load(std::memory_order_acquire);
            if (command_burst != debug_control) {
                debug_control = command_burst;
                ObserveDebugCommand(m_requested_debug_control, command_burst);
                m_debug.SetEnabled(command_burst & 1, now_burst);
                m_telemetry.debug_epoch = static_cast<uint32_t>(command_burst >> 1);
                m_debug_ack.store(command_burst, std::memory_order_release);
                if (m_shm) m_shm->applied_debug_control.store(command_burst, std::memory_order_release);
            }

            // Check config updates while in continuous motion
            const uint64_t observed_burst = block.seq.load(std::memory_order_acquire);
            if (observed_burst != revision && !(observed_burst & 1)) {
                TransformConfig next{};
                if (ReadConfig(block, next, 2) && observed_burst == block.seq.load(std::memory_order_acquire)) {
                    revision = observed_burst;
                    if (ValidTransformConfig(next)) {
                        cfg = next;
                        m_tablet.spec.tip_pressure_threshold = cfg.tip_pressure_threshold;
                        m_pipeline.Configure(m_tablet.spec, cfg);
                        m_telemetry.last_error = 0;
                        if (m_shm) m_shm->applied_config_seq.store(revision, std::memory_order_release);
                    } else m_telemetry.last_error = EINVAL;
                }
            }

            if (now_burst - m_last_hz_calc_ns >= 150000000ULL) {
                if (m_last_hz_calc_ns > 0) {
                    const double dt = (now_burst - m_last_hz_calc_ns) * 1e-9;
                    const uint64_t diff = m_continuous_reports >= m_reports_at_last_calc ? (m_continuous_reports - m_reports_at_last_calc) : 0;
                    const double instant_hz = dt > 0.0 ? (static_cast<double>(diff) / dt) : 0.0;
                    m_continuous_hz = (m_continuous_hz == 0.0) ? instant_hz : (m_continuous_hz * 0.3 + instant_hz * 0.7);
                }
                m_last_hz_calc_ns = now_burst;
                m_reports_at_last_calc = m_continuous_reports;
            }

            if (m_shm && now_burst - last_heartbeat >= 200000000ULL) {
                m_shm->heartbeat_timestamp_ns.store(now_burst, std::memory_order_release);
                last_heartbeat = now_burst;
            }

            if (now_burst - last_publish >= 33333333ULL) {
                Publish(now_burst);
                last_publish = now_burst;
            }

            // Low-latency opportunistic back-to-back hardware report check:
            // Query for immediate packet arrival without CPU-starving busy syscall loops.
            if (has_latest && latest_out.in_range && cfg.spin_percent > 0 && input >= 0) {
                const uint32_t max_spin_iters = static_cast<uint32_t>(cfg.spin_percent * 2);
                bool next_ready = false;
                for (uint32_t spin = 0; spin < max_spin_iters && m_running.load(std::memory_order_relaxed); ++spin) {
                    OSU_CPU_PAUSE();
                    const ssize_t s = m_testing ? recv(input, staged_buffer, sizeof(staged_buffer), MSG_DONTWAIT)
                                                : read(input, staged_buffer, sizeof(staged_buffer));
                    if (s > 0) {
                        staged_size = s;
                        next_ready = true;
                        break;
                    }
                    if (s < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        continue;
                    }
                    if (s < 0 && errno == EINTR) continue;
                    disconnected = true;
                    break;
                }
                if (next_ready) {
                    continue; // Loop directly into next drain-to-latest burst with 0 kernel sleep!
                }
            }
            break; // Spin expired or pen lifted: fall through to outer kernel poll
        }
    }
    if (m_emitter.IsInitialized()) (void)emit(m_pipeline.Lift());
    m_reader.Close();
    m_emitter.Destroy();
    m_pipeline.Reset();
    m_debug.SetEnabled(false);
    m_continuous_reports = 0;
    m_last_hz_calc_ns = 0;
    m_reports_at_last_calc = 0;
    m_continuous_hz = 0.0;
    m_ema_latency_us = 0.0;
    m_session_max_latency_us = 0.0;
    m_ema_jitter_us = 0.0;
    m_last_interval_us = 0.0;
    m_last_packet_arrival_ns = 0;
    m_connected.store(false, std::memory_order_release);
    m_telemetry.connected = m_telemetry.output_ready = 0;
    m_telemetry.evdev_grabbed = 0;
    m_telemetry.pipeline_tid = 0;
    Publish(GetMonotonicTimeNs());
}

} // namespace osupoint
