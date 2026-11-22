#pragma once

#include "osupoint/diagnostics.h"
#include <array>
#include <bit>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <unistd.h>

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define OSU_CPU_PAUSE() _mm_pause()
#elif defined(__aarch64__) || defined(__arm__)
#define OSU_CPU_PAUSE() asm volatile("yield" ::: "memory")
#else
#define OSU_CPU_PAUSE() std::atomic_signal_fence(std::memory_order_seq_cst)
#endif

constexpr uint32_t OSUPOINT_IPC_MAGIC = 0x544E504F;
constexpr uint32_t OSUPOINT_IPC_VERSION = 2;
constexpr const char* OSUPOINT_VERSION = "0.7.0-linux";
constexpr uint64_t OSUPOINT_WRITE_FAILED = UINT64_MAX;

inline uint64_t SetDebugCommand(std::atomic<uint64_t>& control, bool enabled) noexcept {
    uint64_t old = control.load(std::memory_order_acquire);
    for (;;) {
        const uint64_t next = (((old >> 1) + 1) << 1) | uint64_t(enabled);
        if (control.compare_exchange_weak(old, next, std::memory_order_acq_rel)) return next;
    }
}

inline void ObserveDebugCommand(std::atomic<uint64_t>& mirror, uint64_t command) noexcept {
    uint64_t old = mirror.load(std::memory_order_acquire);
    while (old < command && !mirror.compare_exchange_weak(old, command, std::memory_order_release, std::memory_order_relaxed)) {}
}

struct alignas(64) TransformConfig {
    double mx[2] = {}, my[2] = {}, mc[2] = {0.5, 0.5};
    double knx[2] = {}, kny[2] = {}, knc[2] = {};
    double vscale[2] = {65535.0, 65535.0}, vshift[2] = {0.5, 0.5};
    float width_mm = 152, height_mm = 95, cx = 76, cy = 47.5f, rotation_deg = 0;
    int32_t monitor_idx = 0;
    float extrapolation_tau = 0;
    int32_t spin_percent = 0;
    uint8_t clip_area = 0, extrapolation = 0, filter_1euro = 0, filter_motion = 0;
    uint8_t mouse_mode = 1, pen_click = 1, barrel_btn = 1, debug_mode = 0;
    uint32_t debug_epoch = 0;
    uint16_t tip_pressure_threshold = 0;
    uint16_t pressure_max = 8191;
    int32_t output_resolution_x = 1000, output_resolution_y = 1000;
    float user_cutoff = 0, user_v_start = 0, user_v_full = 0;
    uint8_t unlock_limits = 0;
    uint8_t pad[3] = {};
};

// Plain snapshot; only the worker mutates it. IPC transports atomic words rather
// than racing memcpy against an ordinary struct (a seqlock alone isn't C++ safe).
struct alignas(64) DriverTelemetry {
    uint64_t started_ns = 0, total_reports = 0, dropped_reports = 0;
    double current_hz = 0, latency_p50_us = 0, latency_p99_us = 0, latency_max_us = 0, jitter_us = 0;
    double jitter_rfc_us = 0, interval_stddev_us = 0;
    uint16_t vendor_id = 0, product_id = 0;
    uint8_t connected = 0, output_ready = 0, memory_locked = 0, debug_enabled = 0;
    int32_t last_error = 0, pipeline_tid = 0, scheduler_policy = 0, scheduler_priority = 0;
    int32_t initialization_error = 0;
    uint8_t evdev_grabbed = 0;
    int32_t requested_core = -1, affinity_ok = 1, nice_value = 0;
    uint32_t debug_epoch = 0;
    int32_t max_x = 0, max_y = 0;
    double phys_w_mm = 152, phys_h_mm = 95;
    uint8_t report_id = 0, coord_bits = 0, x_offset = 0, y_offset = 0;
    int32_t output_resolution_x = 0, output_resolution_y = 0;
    char device_name[128] = {}, hidraw_path[64] = {}, output_name[64] = {};
    void CopyTo(DriverTelemetry& dest) const noexcept { dest = *this; }
};

template <class T>
struct alignas(64) AtomicSnapshot {
    static_assert(std::is_trivially_copyable_v<T> && sizeof(T) % 8 == 0);
    static_assert(std::atomic<uint64_t>::is_always_lock_free, "64-bit lock-free IPC required");
    std::atomic<uint64_t> seq{0};
    std::array<std::atomic<uint64_t>, sizeof(T) / 8> words{};

    uint64_t Write(const T& value) noexcept {
        uint64_t s = seq.load(std::memory_order_relaxed);
        // Serialize control-plane writers. No writer lock on the input read path.
        bool acquired = false;
        for (unsigned attempt = 0; attempt < 128; ++attempt) {
            if (!(s & 1) && seq.compare_exchange_weak(s, s + 1, std::memory_order_acq_rel)) { acquired = true; break; }
            OSU_CPU_PAUSE();
            s = seq.load(std::memory_order_relaxed);
        }
        if (!acquired) return OSUPOINT_WRITE_FAILED;
        std::array<uint64_t, sizeof(T) / 8> data{};
        T clean = value;
#if defined(__has_builtin)
#if __has_builtin(__builtin_clear_padding)
        __builtin_clear_padding(&clean);
#endif
#endif
        std::memcpy(data.data(), &clean, sizeof(T));
        for (size_t i = 0; i < data.size(); ++i) words[i].store(data[i], std::memory_order_release);
        seq.store(s + 2, std::memory_order_release);
        return s + 2;
    }
    bool Read(T& value, int retries = 10) const noexcept {
        std::array<uint64_t, sizeof(T) / 8> data{};
        while (retries-- > 0) {
            const uint64_t before = seq.load(std::memory_order_acquire);
            if (before & 1) { OSU_CPU_PAUSE(); continue; }
            // Acquiring a newer payload word also observes its writer's odd seq.
            for (size_t i = 0; i < data.size(); ++i) data[i] = words[i].load(std::memory_order_acquire);
            if (seq.load(std::memory_order_acquire) == before) {
                value = std::bit_cast<T>(data);
                return true;
            }
        }
        return false;
    }
};

using ShmConfigBlock = AtomicSnapshot<TransformConfig>;
using ShmTelemetryBlock = AtomicSnapshot<DriverTelemetry>;

struct alignas(64) OsuPointSharedMemory {
    std::atomic<uint32_t> magic{0};
    uint32_t version = OSUPOINT_IPC_VERSION, size = sizeof(OsuPointSharedMemory), daemon_pid = 0;
    std::atomic<uint32_t> gui_pid{0}, command_flags{0};
    std::atomic<uint64_t> heartbeat_timestamp_ns{0}, applied_config_seq{0};
    std::atomic<uint64_t> debug_control{0}, applied_debug_control{0};
    uint8_t reserved[8] = {};
    ShmConfigBlock config_block;
    ShmTelemetryBlock telemetry_block;
    AtomicSnapshot<osupoint::DebugSnapshot> debug_block;
};
static_assert(offsetof(OsuPointSharedMemory, config_block) == 64);

inline bool ReadConfig(const ShmConfigBlock& b, TransformConfig& out, int retries = 10) noexcept { return b.Read(out, retries); }
inline uint64_t WriteConfig(ShmConfigBlock& b, const TransformConfig& in) noexcept { return b.Write(in); }
inline bool ReadTelemetry(const ShmTelemetryBlock& b, DriverTelemetry& out, int retries = 10) noexcept { return b.Read(out, retries); }
inline void WriteTelemetry(ShmTelemetryBlock& b, const DriverTelemetry& in) noexcept { b.Write(in); }
inline void GetIpcShmPath(char* buf, size_t size, uid_t uid = 0) noexcept {
    std::snprintf(buf, size, "/osupoint_ipc_%u", static_cast<unsigned>(uid ? uid : getuid()));
}
