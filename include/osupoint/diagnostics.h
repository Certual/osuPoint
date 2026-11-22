#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace osupoint {

struct DebugPoint {
    float raw_x_mm = 0, raw_y_mm = 0;
    float filtered_x_mm = 0, filtered_y_mm = 0;
};

struct alignas(64) DebugSnapshot {
    uint64_t started_ns = 0, updated_ns = 0, last_packet_ns = 0;
    uint64_t received = 0, decoded = 0, emitted = 0, duplicates = 0;
    uint64_t read_errors = 0, write_errors = 0, reconnects = 0;
    uint64_t decode_reasons[6] = {};
    double input_hz = 0, output_hz = 0;
    double latency_p50_us = 0, latency_p95_us = 0, latency_p99_us = 0;
    double latency_window_max_us = 0, latency_session_max_us = 0;
    double interval_p50_us = 0, interval_p99_us = 0, jitter_us = 0;
    double jitter_rfc_us = 0, interval_stddev_us = 0;
    uint32_t latency_samples = 0, interval_samples = 0, trace_count = 0;
    int32_t last_errno = 0, raw_x = 0, raw_y = 0, out_x = 0, out_y = 0;
    uint16_t pressure = 0;
    uint8_t flags = 0, enabled = 0;
    uint8_t packet_size = 0;
    uint8_t packet[64] = {};
    DebugPoint trace[64] = {};
};

// Owned exclusively by the input worker. No heap, files, timer or background
// collector. Disabling destroys the session, including every retained sample.
class DebugSession {
public:
    static constexpr size_t Capacity = 512;
    void SetEnabled(bool enabled, uint64_t now_ns = 0) noexcept {
        *this = DebugSession{};
        m_snapshot.enabled = enabled;
        if (enabled) {
            m_snapshot.started_ns = now_ns;
            m_rate_time = now_ns;
        }
    }
    bool Enabled() const noexcept { return m_snapshot.enabled != 0; }
    void Receive(uint64_t now_ns, const uint8_t* packet, size_t size) noexcept {
        if (!Enabled()) return;
        ++m_snapshot.received;
        if (m_snapshot.last_packet_ns && now_ns > m_snapshot.last_packet_ns) {
            const uint64_t interval = now_ns - m_snapshot.last_packet_ns;
            if (interval < 100000000ULL) {
                m_intervals[m_interval_pos++ & (Capacity - 1)] = interval;
                m_interval_count = std::min(m_interval_count + 1, Capacity);
                const double intv_us = interval * 0.001;
                if (m_last_interval_us > 0.0) {
                    const double d = std::abs(intv_us - m_last_interval_us);
                    m_rfc_jitter_us += (d - m_rfc_jitter_us) / 16.0;
                }
                m_last_interval_us = intv_us;
            } else {
                m_trace.fill({}); m_trace_count = m_trace_pos = 0;
            }
        }
        m_snapshot.last_packet_ns = now_ns;
        m_snapshot.packet_size = static_cast<uint8_t>(std::min(size, sizeof(m_snapshot.packet)));
        std::fill(std::begin(m_snapshot.packet), std::end(m_snapshot.packet), 0);
        std::copy_n(packet, m_snapshot.packet_size, m_snapshot.packet);
    }
    void Decode(unsigned reason) noexcept {
        if (!Enabled()) return;
        if (reason < 6) ++m_snapshot.decode_reasons[reason];
        if (reason == 0) ++m_snapshot.decoded;
    }
    void Output(uint64_t elapsed_ns, bool emitted, bool duplicate,
                int error, int32_t raw_x, int32_t raw_y, int32_t x, int32_t y,
                uint16_t pressure, uint8_t flags, const DebugPoint& point, bool motion_sample = true) noexcept {
        if (!Enabled()) return;
        m_snapshot.raw_x = raw_x; m_snapshot.raw_y = raw_y;
        m_snapshot.out_x = x; m_snapshot.out_y = y;
        m_snapshot.pressure = pressure; m_snapshot.flags = flags;
        if (duplicate) ++m_snapshot.duplicates;
        else if (emitted) ++m_snapshot.emitted;
        else { ++m_snapshot.write_errors; m_snapshot.last_errno = error; }
        if (emitted) {
            m_latencies[m_latency_pos++ & (Capacity - 1)] = elapsed_ns;
            m_latency_count = std::min(m_latency_count + 1, Capacity);
            m_snapshot.latency_session_max_us = std::max(m_snapshot.latency_session_max_us, elapsed_ns * 0.001);
        }
        if (motion_sample) {
            m_trace[m_trace_pos++ & 63] = point;
            m_trace_count = std::min(m_trace_count + 1, size_t{64});
        } else {
            m_trace.fill({}); m_trace_count = m_trace_pos = 0;
        }
    }
    void ReadError(int error) noexcept {
        if (Enabled()) { ++m_snapshot.read_errors; m_snapshot.last_errno = error; }
    }
    void Reconnect() noexcept { if (Enabled()) ++m_snapshot.reconnects; }
    DebugSnapshot Snapshot(uint64_t now_ns) noexcept {
        if (!Enabled()) return {};
        m_snapshot.updated_ns = now_ns;
        if (now_ns > m_rate_time) {
            const double seconds = (now_ns - m_rate_time) * 1e-9;
            m_snapshot.input_hz = (m_snapshot.received - m_rate_received) / seconds;
            m_snapshot.output_hz = (m_snapshot.emitted - m_rate_emitted) / seconds;
            m_rate_time = now_ns;
            m_rate_received = m_snapshot.received;
            m_rate_emitted = m_snapshot.emitted;
        }
        auto latencies = m_latencies;
        auto intervals = m_intervals;
        std::sort(latencies.begin(), latencies.begin() + m_latency_count);
        std::sort(intervals.begin(), intervals.begin() + m_interval_count);
        const auto percentile = [](const auto& values, size_t count, double p) {
            if (!count) return 0.0;
            const size_t index = static_cast<size_t>(std::ceil(p * count)) - 1;
            return values[std::min(index, count - 1)] * 0.001;
        };
        m_snapshot.latency_samples = static_cast<uint32_t>(m_latency_count);
        m_snapshot.interval_samples = static_cast<uint32_t>(m_interval_count);
        m_snapshot.latency_p50_us = percentile(latencies, m_latency_count, 0.50);
        m_snapshot.latency_p95_us = percentile(latencies, m_latency_count, 0.95);
        m_snapshot.latency_p99_us = percentile(latencies, m_latency_count, 0.99);
        m_snapshot.latency_window_max_us = percentile(latencies, m_latency_count, 1.0);
        m_snapshot.interval_p50_us = percentile(intervals, m_interval_count, 0.50);
        m_snapshot.interval_p99_us = percentile(intervals, m_interval_count, 0.99);
        m_snapshot.jitter_us = m_snapshot.interval_p99_us - m_snapshot.interval_p50_us;
        m_snapshot.jitter_rfc_us = m_rfc_jitter_us;
        if (m_interval_count >= 2) {
            double sum = 0.0;
            for (size_t i = 0; i < m_interval_count; ++i) {
                sum += intervals[i] * 0.001;
            }
            const double mean = sum / m_interval_count;
            double var = 0.0;
            for (size_t i = 0; i < m_interval_count; ++i) {
                const double diff = (intervals[i] * 0.001) - mean;
                var += diff * diff;
            }
            m_snapshot.interval_stddev_us = std::sqrt(var / m_interval_count);
        } else {
            m_snapshot.interval_stddev_us = 0.0;
        }
        m_snapshot.trace_count = static_cast<uint32_t>(m_trace_count);
        for (size_t i = 0; i < m_trace_count; ++i)
            m_snapshot.trace[i] = m_trace[(m_trace_pos - m_trace_count + i) & 63];
        return m_snapshot;
    }
private:
    DebugSnapshot m_snapshot{};
    std::array<uint64_t, Capacity> m_latencies{}, m_intervals{};
    std::array<DebugPoint, 64> m_trace{};
    size_t m_latency_pos = 0, m_interval_pos = 0, m_trace_pos = 0;
    size_t m_latency_count = 0, m_interval_count = 0, m_trace_count = 0;
    uint64_t m_rate_time = 0, m_rate_received = 0, m_rate_emitted = 0;
    double m_last_interval_us = 0.0;
    double m_rfc_jitter_us = 0.0;
};

} // namespace osupoint
