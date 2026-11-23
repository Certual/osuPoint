#pragma once
#include "common.h"
#include "tablet_spec.h"
#include <cstring>

struct alignas(64) RawPacket {
    uint8_t  data[48];
    uint32_t len = 0;
    uint32_t endpointIdx = 0;
    uint64_t qpc = 0;
};
static_assert(sizeof(RawPacket) == 64, "RawPacket must fit in a single 64-byte cache line");

template <typename T, size_t Capacity>
class SpscRing {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
public:
    SpscRing() : m_head(0), m_tailCache(0), m_tail(0), m_headCache(0) {}

    bool TryPush(const T& item) noexcept {
        const size_t head = m_head.load(std::memory_order_relaxed);
        if (head - m_tailCache >= Capacity) {
            m_tailCache = m_tail.load(std::memory_order_acquire);
            if (head - m_tailCache >= Capacity) {
                return false;
            }
        }
        m_buffer[head & (Capacity - 1)] = item;
        m_head.store(head + 1, std::memory_order_release);
        return true;
    }

    bool TryPush(const void* srcData, uint32_t len, uint32_t epIdx, uint64_t qpc) noexcept {
        const size_t head = m_head.load(std::memory_order_relaxed);
        if (head - m_tailCache >= Capacity) {
            m_tailCache = m_tail.load(std::memory_order_acquire);
            if (head - m_tailCache >= Capacity) {
                return false;
            }
        }
        T& dest = m_buffer[head & (Capacity - 1)];
        const uint32_t toCopy = (len > sizeof(dest.data)) ? static_cast<uint32_t>(sizeof(dest.data)) : len;
        const auto* s = static_cast<const __m128i*>(srcData);
        auto* d = reinterpret_cast<__m128i*>(dest.data);
        _mm_store_si128(&d[0], _mm_loadu_si128(&s[0]));
        _mm_store_si128(&d[1], _mm_loadu_si128(&s[1]));
        _mm_store_si128(&d[2], _mm_loadu_si128(&s[2]));
        dest.len = toCopy;
        dest.endpointIdx = epIdx;
        dest.qpc = qpc;
        m_head.store(head + 1, std::memory_order_release);
        return true;
    }

    bool TryPop(T& item) noexcept {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_headCache) {
            const size_t head = m_head.load(std::memory_order_acquire);
            if (tail == head) {
                return false;
            }
            m_headCache = head;
        }
        item = m_buffer[tail & (Capacity - 1)];
        m_tail.store(tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] const T* Peek() const noexcept {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_headCache) {
            const size_t head = m_head.load(std::memory_order_acquire);
            if (tail == head) {
                return nullptr;
            }
            m_headCache = head;
        }
        return &m_buffer[tail & (Capacity - 1)];
    }

    [[nodiscard]] T* Peek() noexcept {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_headCache) {
            const size_t head = m_head.load(std::memory_order_acquire);
            if (tail == head) {
                return nullptr;
            }
            m_headCache = head;
        }
        return &m_buffer[tail & (Capacity - 1)];
    }

    void Pop() noexcept {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        m_tail.store(tail + 1, std::memory_order_release);
    }

    [[nodiscard]] bool Empty() const noexcept {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_headCache) {
            const size_t head = m_head.load(std::memory_order_acquire);
            if (tail == head) {
                return true;
            }
            m_headCache = head;
        }
        return false;
    }

    [[nodiscard]] size_t Available() const noexcept {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        const size_t head = m_head.load(std::memory_order_acquire);
        if (m_headCache != head) {
            m_headCache = head;
        }
        return (head >= tail) ? (head - tail) : 0;
    }

private:
    alignas(64) std::atomic<size_t> m_head{ 0 };
    alignas(64) size_t m_tailCache{ 0 };

    alignas(64) std::atomic<size_t> m_tail{ 0 };
    alignas(64) mutable size_t m_headCache{ 0 };

    alignas(64) T m_buffer[Capacity]{};
};

class alignas(64) AtomicConfigStore {
public:
    void Store(const TransformConfig& newCfg) noexcept {
        uint32_t s = m_seq.load(std::memory_order_relaxed);
        m_seq.store(s + 1, std::memory_order_release);
        _ReadWriteBarrier();
        m_cfg = newCfg;
        _ReadWriteBarrier();
        m_seq.store(s + 2, std::memory_order_release);
    }

    TransformConfig Load(uint32_t* outSeq = nullptr) const noexcept {
        TransformConfig result;
        uint32_t s1 = 0, s2 = 0;
        do {
            s1 = m_seq.load(std::memory_order_acquire);
            while (s1 & 1) {
                _mm_pause();
                s1 = m_seq.load(std::memory_order_acquire);
            }
            _ReadWriteBarrier();
            result = m_cfg;
            _ReadWriteBarrier();
            std::atomic_thread_fence(std::memory_order_acquire);
            s2 = m_seq.load(std::memory_order_relaxed);
        } while (s1 != s2);
        if (outSeq) *outSeq = s1;
        return result;
    }

    [[nodiscard]] inline uint32_t GetSequence() const noexcept {
        return m_seq.load(std::memory_order_relaxed);
    }

private:
    std::atomic<uint32_t> m_seq{ 0 };
    TransformConfig m_cfg{};
};

class alignas(64) AtomicSpecStore {
public:
    void Store(const RuntimeTabletSpec& newSpec) noexcept {
        uint32_t s = m_seq.load(std::memory_order_relaxed);
        m_seq.store(s + 1, std::memory_order_release);
        _ReadWriteBarrier();
        m_spec = newSpec;
        _ReadWriteBarrier();
        m_seq.store(s + 2, std::memory_order_release);
    }

    RuntimeTabletSpec Load(uint32_t* outSeq = nullptr) const noexcept {
        RuntimeTabletSpec result;
        uint32_t s1 = 0, s2 = 0;
        do {
            s1 = m_seq.load(std::memory_order_acquire);
            while (s1 & 1) {
                _mm_pause();
                s1 = m_seq.load(std::memory_order_acquire);
            }
            _ReadWriteBarrier();
            result = m_spec;
            _ReadWriteBarrier();
            std::atomic_thread_fence(std::memory_order_acquire);
            s2 = m_seq.load(std::memory_order_relaxed);
        } while (s1 != s2);
        if (outSeq) *outSeq = s1;
        return result;
    }

    [[nodiscard]] inline uint32_t GetSequence() const noexcept {
        return m_seq.load(std::memory_order_relaxed);
    }

private:
    std::atomic<uint32_t> m_seq{ 0 };
    RuntimeTabletSpec m_spec{};
};

struct LatencyStats {
    static constexpr size_t HIST_CAP = 512;
    std::atomic<uint64_t> count{ 0 };
    std::atomic<uint32_t> lastUs{ 0 };
    std::atomic<uint32_t> maxUs{ 0 };
    std::atomic<uint32_t> p50Us{ 0 };
    std::atomic<uint32_t> p99Us{ 0 };
    std::atomic<uint32_t> minUs{ 0 };
    std::atomic<uint32_t> writeIdx{ 0 };
    std::atomic<uint32_t> samples[HIST_CAP]{};

    void Reset() noexcept {
        count.store(0, std::memory_order_relaxed);
        lastUs.store(0, std::memory_order_relaxed);
        maxUs.store(0, std::memory_order_relaxed);
        p50Us.store(0, std::memory_order_relaxed);
        p99Us.store(0, std::memory_order_relaxed);
        minUs.store(0, std::memory_order_relaxed);
        writeIdx.store(0, std::memory_order_relaxed);
        for (size_t i = 0; i < HIST_CAP; ++i) {
            samples[i].store(0, std::memory_order_relaxed);
        }
    }

    inline void Record(uint64_t us) noexcept {
        uint32_t u = static_cast<uint32_t>(us);
        lastUs.store(u, std::memory_order_relaxed);
        count.fetch_add(1, std::memory_order_relaxed);

        uint32_t curMax = maxUs.load(std::memory_order_relaxed);
        while (u > curMax && !maxUs.compare_exchange_weak(curMax, u, std::memory_order_relaxed)) {}

        uint32_t idx = writeIdx.fetch_add(1, std::memory_order_relaxed);
        samples[idx & (HIST_CAP - 1)].store(u, std::memory_order_relaxed);
    }

    void Compute(uint32_t& out_p50, uint32_t& out_p99, uint32_t& out_max, uint32_t& out_min) noexcept {
        uint32_t total = writeIdx.load(std::memory_order_relaxed);
        if (total == 0) {
            out_p50 = out_p99 = out_max = out_min = 0;
            return;
        }

        uint32_t n = (std::min)(total, static_cast<uint32_t>(HIST_CAP));
        uint32_t sorted[HIST_CAP];
        uint32_t startIdx = total - n;
        for (uint32_t i = 0; i < n; ++i) {
            sorted[i] = samples[(startIdx + i) & (HIST_CAP - 1)].load(std::memory_order_relaxed);
        }
        std::sort(sorted, sorted + n);

        out_min = sorted[0];
        out_p50 = sorted[n * 50 / 100];
        out_p99 = sorted[n * 99 / 100];
        out_max = sorted[n - 1];

        minUs.store(out_min, std::memory_order_relaxed);
        p50Us.store(out_p50, std::memory_order_relaxed);
        p99Us.store(out_p99, std::memory_order_relaxed);
        maxUs.store(out_max, std::memory_order_relaxed);
    }
};

struct JitterStats {
    static constexpr size_t HIST_CAP = 512;
    std::atomic<uint64_t> count{ 0 };
    std::atomic<uint32_t> writeIdx{ 0 };
    std::atomic<uint32_t> jitterUs{ 0 };
    std::atomic<uint32_t> stdDevUs{ 0 };
    std::atomic<uint32_t> avgIntervalUs{ 0 };
    std::atomic<uint32_t> intervals[HIST_CAP]{};
    uint32_t lastIntervalUs{ 0 };
    double rfcJitter{ 0.0 };

    void Reset() noexcept {
        count.store(0, std::memory_order_relaxed);
        writeIdx.store(0, std::memory_order_relaxed);
        jitterUs.store(0, std::memory_order_relaxed);
        stdDevUs.store(0, std::memory_order_relaxed);
        avgIntervalUs.store(0, std::memory_order_relaxed);
        lastIntervalUs = 0;
        rfcJitter = 0.0;
        for (size_t i = 0; i < HIST_CAP; ++i) {
            intervals[i].store(0, std::memory_order_relaxed);
        }
    }

    inline void RecordInterval(uint64_t intvUs) noexcept {
        uint32_t u = static_cast<uint32_t>(intvUs);
        if (lastIntervalUs > 0) {
            double d = std::abs(static_cast<double>(u) - static_cast<double>(lastIntervalUs));
            rfcJitter += (d - rfcJitter) / 16.0;
            jitterUs.store(static_cast<uint32_t>(rfcJitter + 0.5), std::memory_order_relaxed);
        }
        lastIntervalUs = u;
        uint32_t idx = writeIdx.fetch_add(1, std::memory_order_relaxed);
        intervals[idx & (HIST_CAP - 1)].store(u, std::memory_order_relaxed);
        count.fetch_add(1, std::memory_order_relaxed);
    }

    [[nodiscard]] inline uint32_t GetJitterUs() const noexcept {
        return jitterUs.load(std::memory_order_relaxed);
    }

    void Compute(uint32_t& out_stdDevUs, uint32_t& out_avgIntvUs) noexcept {
        uint32_t total = writeIdx.load(std::memory_order_relaxed);
        if (total < 2) {
            out_stdDevUs = 0;
            out_avgIntvUs = (total == 1) ? intervals[0].load(std::memory_order_relaxed) : 0;
            return;
        }

        uint32_t n = (std::min)(total, static_cast<uint32_t>(HIST_CAP));
        uint32_t startIdx = total - n;
        double sum = 0.0;
        for (uint32_t i = 0; i < n; ++i) {
            sum += intervals[(startIdx + i) & (HIST_CAP - 1)].load(std::memory_order_relaxed);
        }
        double mean = sum / n;
        double varSum = 0.0;
        for (uint32_t i = 0; i < n; ++i) {
            double diff = intervals[(startIdx + i) & (HIST_CAP - 1)].load(std::memory_order_relaxed) - mean;
            varSum += diff * diff;
        }
        double stdDev = std::sqrt(varSum / n);
        out_stdDevUs = static_cast<uint32_t>(stdDev + 0.5);
        out_avgIntvUs = static_cast<uint32_t>(mean + 0.5);
        stdDevUs.store(out_stdDevUs, std::memory_order_relaxed);
        avgIntervalUs.store(out_avgIntvUs, std::memory_order_relaxed);
    }
};

struct WacomDiagnosticStats {
    std::atomic<uint64_t> accepted{0};
    std::atomic<uint64_t> rejected{0};
    std::atomic<uint64_t> glitches{0};
};

struct PrevPacketData {
    uint64_t qpc{0};
    int32_t x = 0;
    int32_t y = 0;
    int len = 0;
    BYTE data[16]{};
};

struct FilterParams {
    bool enabled = false;
    double min_cutoff = 70.0;
    double v_start = 15.0;
    double v_full = 65.0;
    double user_cutoff = 0.0;
    double user_v_start = 0.0;
    double user_v_full = 0.0;
};

inline void ComputeFilterParams(int filterPercent, FilterParams& p) noexcept {
    p.enabled = (filterPercent > 0);
    if (filterPercent <= 0) {
        p.min_cutoff = 1000.0;
    } else if (filterPercent <= 100) {
        p.min_cutoff = 70.0 - 0.56 * static_cast<double>(filterPercent);
    } else {
        double f = static_cast<double>(filterPercent) * 0.01;
        p.min_cutoff = (std::max)(14.0 / std::sqrt(f), 3.0);
    }
    p.v_start = 15.0;
    p.v_full = 65.0;
    if (p.user_cutoff > 0.0) p.min_cutoff = p.user_cutoff;
    if (p.user_v_start > 0.0) p.v_start = p.user_v_start;
    if (p.user_v_full > 0.0) p.v_full = p.user_v_full;
}

inline double SmoothStep(double edge0, double edge1, double x) noexcept {
    if (edge1 <= edge0) return (x >= edge1) ? 1.0 : 0.0;
    double t = (std::clamp)((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

inline double ComputeAlpha(double dt, double cutoff_hz, double factor = 6.283185307179586) noexcept {
    double safe_dt = (dt < 0.0001) ? 0.0001 : dt;
    double tau = factor * cutoff_hz * safe_dt;
    if (std::isnan(tau) || tau > 1e12) return 1.0;
    double alpha = tau / (1.0 + tau);
    return (std::clamp)(alpha, 0.0, 1.0);
}

class AdaptiveFilterPipeline {
public:
    void Reset() noexcept {
        m_initialized = false;
        m_activeLead = false;
        m_prevX = 0.0;
        m_prevY = 0.0;
        m_prevRawX = 0.0;
        m_prevRawY = 0.0;
        m_prevDx = 0.0;
        m_prevDy = 0.0;
        m_derivMag = 0.0;
        m_filteredDx = 0.0;
        m_filteredDy = 0.0;
        m_filteredVx = 0.0;
        m_filteredVy = 0.0;
        m_velocityMag = 0.0;
        m_prevSpeed = 0.0;
        m_warmupTicks = 10;
        m_lastQpc = 0;
    }

    void Filter(double rawX, double rawY, double dt, const FilterParams& params,
                double extrapolation_mm, double& outX, double& outY) noexcept {
        if (std::isnan(rawX) || std::isnan(rawY)) {
            outX = rawX;
            outY = rawY;
            return;
        }

        if (!params.enabled || dt <= 0.0 || dt > 1.0) {
            outX = rawX;
            outY = rawY;
            m_initialized = true;
            m_prevX = rawX;
            m_prevY = rawY;
            m_prevRawX = rawX;
            m_prevRawY = rawY;
            m_warmupTicks = 10;
            return;
        }

        if (!m_initialized) {
            m_initialized = true;
            m_prevX = rawX;
            m_prevY = rawY;
            m_prevRawX = rawX;
            m_prevRawY = rawY;
            m_prevDx = 0.0;
            m_prevDy = 0.0;
            m_derivMag = 0.0;
            m_filteredDx = 0.0;
            m_filteredDy = 0.0;
            m_filteredVx = 0.0;
            m_filteredVy = 0.0;
            m_velocityMag = 0.0;
            m_prevSpeed = 0.0;
            m_warmupTicks = 10;
            outX = rawX;
            outY = rawY;
            return;
        }

        double safe_dt = (dt < 0.0001) ? 0.0001 : dt;
        double dx = (rawX - m_prevRawX) / safe_dt;
        double dy = (rawY - m_prevRawY) / safe_dt;
        m_prevRawX = rawX;
        m_prevRawY = rawY;

        double alpha_d = ComputeAlpha(safe_dt, 10.0, 62.83185307179586);
        m_filteredDx = (1.0 - alpha_d) * m_filteredDx + alpha_d * dx;
        m_filteredDy = (1.0 - alpha_d) * m_filteredDy + alpha_d * dy;
        double speed = std::sqrt(m_filteredDx * m_filteredDx + m_filteredDy * m_filteredDy);

        double cutoff = params.min_cutoff * 0.8;
        if (cutoff < 12.0) cutoff = 12.0;
        if (cutoff > 28.0) cutoff = 28.0;

        double alpha_p = ComputeAlpha(safe_dt, cutoff, 6.283185307179586);

        double motion_factor = SmoothStep(params.v_start, params.v_full, speed);
        if (m_warmupTicks > 0) {
            m_warmupTicks--;
            motion_factor = 0.0;
        }

        double effective_alpha = (1.0 - motion_factor) * alpha_p + motion_factor * 1.0;
        double x_filt = (1.0 - effective_alpha) * m_prevX + effective_alpha * rawX;
        double y_filt = (1.0 - effective_alpha) * m_prevY + effective_alpha * rawY;
        m_prevX = x_filt;
        m_prevY = y_filt;

        if (extrapolation_mm > 0.0 && speed > 1e-4) {
            double lead_factor = extrapolation_mm / speed;
            outX = x_filt + m_filteredDx * lead_factor;
            outY = y_filt + m_filteredDy * lead_factor;
            m_activeLead = true;
        } else {
            outX = x_filt;
            outY = y_filt;
            m_activeLead = false;
        }
    }

    [[nodiscard]] bool HasActiveLead() const noexcept {
        return m_activeLead;
    }

    void Update(double inX, double inY, uint64_t nowQpc, uint64_t qpcFreq,
                const FilterParams& params, double extrapolation_mm,
                double& outX, double& outY) noexcept {
        double dt_sec = 0.0;
        if (qpcFreq > 0 && m_lastQpc > 0 && nowQpc > m_lastQpc) {
            dt_sec = static_cast<double>(nowQpc - m_lastQpc) / static_cast<double>(qpcFreq);
        }
        if (!m_initialized || dt_sec >= 0.0001 || m_lastQpc == 0 || nowQpc < m_lastQpc) {
            m_lastQpc = nowQpc;
        }
        Filter(inX, inY, dt_sec, params, extrapolation_mm, outX, outY);
    }

private:
    bool   m_initialized = false;
    double m_prevX = 0.0;
    double m_prevY = 0.0;
    double m_prevRawX = 0.0;
    double m_prevRawY = 0.0;
    double m_prevDx = 0.0;
    double m_prevDy = 0.0;
    double m_derivMag = 0.0;
    double m_filteredDx = 0.0;
    double m_filteredDy = 0.0;
    double m_filteredVx = 0.0;
    double m_filteredVy = 0.0;
    double m_velocityMag = 0.0;
    double m_prevSpeed = 0.0;
    int    m_warmupTicks = 10;
    bool   m_activeLead = false;
    uint64_t m_lastQpc = 0;
};

struct ExtrapolationConfig {
    bool enabled = false;
    double tau_ms = 2.5;
};

class KinematicExtrapolator {
public:
    void Reset() noexcept {
        m_hasPos = false;
        m_hasVel = false;
        m_activeLead = false;
        m_lastX = 0.0;
        m_lastY = 0.0;
        m_lastVx = 0.0;
        m_lastVy = 0.0;
        m_lastQpc = 0;
    }

    [[nodiscard]] bool HasActiveLead() const noexcept {
        return m_activeLead;
    }

    void UpdateWithDt(double inX, double inY, double dt_ms, const ExtrapolationConfig& cfg,
                      double& outX, double& outY) noexcept {
        if (!cfg.enabled || cfg.tau_ms <= 0.0) {
            outX = inX;
            outY = inY;
            Reset();
            return;
        }

        const double tau = (std::clamp)(cfg.tau_ms, 1.0, 5.0);

        if (m_hasPos && dt_ms >= 0.0 && dt_ms < 0.1) {
            m_lastX = inX;
            m_lastY = inY;
            if (m_hasVel && m_activeLead) {
                outX = inX + m_lastVx * tau;
                outY = inY + m_lastVy * tau;
            } else {
                outX = inX;
                outY = inY;
            }
            return;
        }

        if (!m_hasPos || dt_ms < 0.1 || dt_ms > 40.0) {
            m_hasPos = true;
            m_hasVel = false;
            m_activeLead = false;
            m_lastX = inX;
            m_lastY = inY;
            m_lastVx = 0.0;
            m_lastVy = 0.0;
            outX = inX;
            outY = inY;
            return;
        }

        const double invDt = 1.0 / dt_ms;
        const double vx = (inX - m_lastX) * invDt;
        const double vy = (inY - m_lastY) * invDt;
        const double curSpeedSq = vx * vx + vy * vy;

        constexpr double NOISE_SPEED_SQ = 0.0025; // speed < 0.05
        if (curSpeedSq < NOISE_SPEED_SQ) {
            m_lastX = inX;
            m_lastY = inY;
            m_lastVx = 0.0;
            m_lastVy = 0.0;
            m_hasVel = true;
            m_activeLead = false;
            outX = inX;
            outY = inY;
            return;
        }

        if (!m_hasVel) {
            m_hasVel = true;
            m_activeLead = true;
            m_lastX = inX;
            m_lastY = inY;
            m_lastVx = vx;
            m_lastVy = vy;
            outX = inX + vx * tau;
            outY = inY + vy * tau;
            return;
        }

        // Overshoot & Turnaround suppression:
        // 1. Sharp turnaround: dot product of current and previous velocity < 0
        const double prevSpeedSq = m_lastVx * m_lastVx + m_lastVy * m_lastVy;
        const double dot = vx * m_lastVx + vy * m_lastVy;
        bool suppress = (prevSpeedSq >= NOISE_SPEED_SQ && dot < 0.0);

        // 2. Sharp braking / deceleration:
        const double ax = (vx - m_lastVx) * invDt;
        const double ay = (vy - m_lastVy) * invDt;
        const double vprojX = vx + ax * tau;
        const double vprojY = vy + ay * tau;
        const double projDot = vprojX * vx + vprojY * vy;

        if (projDot <= 0.0 || (curSpeedSq < 0.12 * prevSpeedSq && prevSpeedSq > 0.25)) {
            suppress = true;
        }

        double leadX = 0.0;
        double leadY = 0.0;
        if (!suppress) {
            leadX = vx * tau + 0.5 * ax * tau * tau;
            leadY = vy * tau + 0.5 * ay * tau * tau;

            const double accelDotVel = ax * vx + ay * vy;
            if (accelDotVel < 0.0 && curSpeedSq > 1e-12) {
                const double brakingGain = (std::clamp)(projDot / curSpeedSq, 0.0, 1.0);
                leadX *= brakingGain;
                leadY *= brakingGain;
            }

            const double leadDotVel = leadX * vx + leadY * vy;
            if (leadDotVel <= 0.0) {
                leadX = 0.0;
                leadY = 0.0;
            } else {
                const double maxLeadSq = (2.25 * tau * tau) * curSpeedSq;
                const double leadMagSq = leadX * leadX + leadY * leadY;
                if (leadMagSq > maxLeadSq && leadMagSq > 1e-12) [[unlikely]] {
                    const double scale = std::sqrt(maxLeadSq / leadMagSq);
                    leadX *= scale;
                    leadY *= scale;
                }
            }
        }

        m_lastX = inX;
        m_lastY = inY;
        m_lastVx = vx;
        m_lastVy = vy;
        m_activeLead = (leadX != 0.0 || leadY != 0.0);

        outX = inX + leadX;
        outY = inY + leadY;
    }

    void Update(double inX, double inY, uint64_t nowQpc, uint64_t qpcFreq,
                const ExtrapolationConfig& cfg, double& outX, double& outY) noexcept {
        double dt_ms = 0.0;
        if (qpcFreq > 0 && m_lastQpc > 0 && nowQpc > m_lastQpc) {
            if (g_qpcToUs > 0.0) {
                dt_ms = static_cast<double>(nowQpc - m_lastQpc) * g_qpcToUs * 0.001;
            } else {
                dt_ms = static_cast<double>(nowQpc - m_lastQpc) * 1000.0 / static_cast<double>(qpcFreq);
            }
        }
        if (!m_hasPos || dt_ms >= 0.1 || m_lastQpc == 0 || nowQpc < m_lastQpc) {
            m_lastQpc = nowQpc;
        }
        UpdateWithDt(inX, inY, dt_ms, cfg, outX, outY);
    }

private:
    bool m_hasPos = false;
    bool m_hasVel = false;
    bool m_activeLead = false;
    double m_lastX = 0.0;
    double m_lastY = 0.0;
    double m_lastVx = 0.0;
    double m_lastVy = 0.0;
    uint64_t m_lastQpc = 0;
};

// --- Core Driver Configuration ---
extern TabletSpec g_spec;
extern AtomicSpecStore g_specStore;
extern AtomicConfigStore g_cfgStore;
extern std::atomic<bool> g_penClick;
extern std::atomic<bool> g_barrelClick;
extern bool g_clipArea;
extern bool g_calibrate;
extern bool g_extrapolation;
extern double g_extrapolationTau;
extern double g_extrapolationMm;
extern int g_filterPercent;
extern int g_motionFilterPercent;
extern double g_mf_cutoff;
extern double g_mf_v_start;
extern double g_mf_v_full;
extern int g_spinPercent;
extern bool g_unlockLimits;
extern std::string g_dumpReportsFile;
extern std::atomic<bool> g_sharedModeFallback;

// --- Diagnostics & Performance Stats ---
struct CompactPenSample {
    int32_t raw_x = 0;
    int32_t raw_y = 0;
    int32_t px = 0;
    int32_t py = 0;
    int32_t si_x = 0;
    int32_t si_y = 0;
    double  un = 0.0;
    double  vn = 0.0;
    uint16_t pressure = 0;
    uint8_t  tip = 0;
    uint8_t  barrel = 0;
    uint8_t  in_range = 0;
};
struct LivePenTelemetry {
    static constexpr size_t TRAJ_CAP = 4;
    std::atomic<uint32_t> write_idx{ 0 };
    CompactPenSample current{};
    CompactPenSample history[TRAJ_CAP]{};
};
extern LivePenTelemetry g_livePen;
extern LatencyStats g_latencyStats;
extern JitterStats g_jitterStats;
extern WacomDiagnosticStats g_wacomStats;
extern LARGE_INTEGER g_qpcFreq;
extern double g_qpcToUs;
extern alignas(64) std::atomic<uint64_t> g_reportsReceived;
extern alignas(64) std::atomic<uint64_t> g_rawReportsReceived;
extern alignas(64) std::atomic<uint64_t> g_reportsDeduped;
extern alignas(64) std::atomic<uint32_t> g_reconnectCount;
extern alignas(64) std::atomic<DWORD>    g_lastReadError;
extern alignas(64) std::atomic<uint32_t> g_lastReportBytes;
extern alignas(64) std::atomic<int>      g_numActiveEndpoints;
extern alignas(64) std::atomic<uint64_t> g_epCount[4];

// --- Device Handles & Notifications ---
extern std::atomic<HANDLE> g_hDevice;
extern HANDLE g_hDeviceChangeEvent;
extern HDEVNOTIFY g_hDevNotify;

// --- Transform & Hardware Initialization ---
void SetTransform(double w, double h, double cx, double cy, double rot_deg, int monitor_idx) noexcept;
std::vector<std::string> DetectAndInitTablet(bool list_devices = false);
HANDLE OpenTabletHandle(const std::string& devPath, bool isPrimary = true) noexcept;

// --- Pipeline Lifecycle & Worker Threads ---
extern std::jthread g_readerThread;
extern std::jthread g_dispatchThread;
void StartDriverPipeline();
void StopDriverPipeline();
void ReaderThread(std::stop_token stoken);
void InputDispatchThread(std::stop_token stoken);

// PointJIT completely removed: static no-op stubs for UI/telemetry compatibility
inline bool InitPointJitEngine() noexcept { return false; }
inline void ShutdownPointJitEngine() noexcept {}
inline void CheckPointJitIpcUpdates() noexcept {}
inline bool IsPointJitActive() noexcept { return false; }
inline double GetPointJitLastNs() noexcept { return 0.0; }
inline uint32_t GetPointJitCodeSize() noexcept { return 0; }
