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
    SpscRing() : m_head(0), m_tail(0) {}

    bool TryPush(const T& item) noexcept {
        const size_t head = m_head.load(std::memory_order_relaxed);
        const size_t tail = m_tail.load(std::memory_order_acquire);
        if (head - tail >= Capacity) {
            return false;
        }
        m_buffer[head & (Capacity - 1)] = item;
        m_head.store(head + 1, std::memory_order_release);
        return true;
    }

    bool TryPush(const void* srcData, uint32_t len, uint32_t epIdx, uint64_t qpc) noexcept {
        const size_t head = m_head.load(std::memory_order_relaxed);
        const size_t tail = m_tail.load(std::memory_order_acquire);
        if (head - tail >= Capacity) {
            return false;
        }
        T& dest = m_buffer[head & (Capacity - 1)];
        uint32_t toCopy = (len > sizeof(dest.data)) ? static_cast<uint32_t>(sizeof(dest.data)) : len;
        std::memcpy(dest.data, srcData, toCopy);
        dest.len = toCopy;
        dest.endpointIdx = epIdx;
        dest.qpc = qpc;
        m_head.store(head + 1, std::memory_order_release);
        return true;
    }

    bool TryPop(T& item) noexcept {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        const size_t head = m_head.load(std::memory_order_acquire);
        if (tail == head) {
            return false;
        }
        item = m_buffer[tail & (Capacity - 1)];
        m_tail.store(tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] const T* Peek() const noexcept {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        const size_t head = m_head.load(std::memory_order_acquire);
        if (tail == head) {
            return nullptr;
        }
        return &m_buffer[tail & (Capacity - 1)];
    }

    [[nodiscard]] T* Peek() noexcept {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        const size_t head = m_head.load(std::memory_order_acquire);
        if (tail == head) {
            return nullptr;
        }
        return &m_buffer[tail & (Capacity - 1)];
    }

    void Pop() noexcept {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        m_tail.store(tail + 1, std::memory_order_release);
    }

    [[nodiscard]] bool Empty() const noexcept {
        return m_tail.load(std::memory_order_relaxed) == m_head.load(std::memory_order_relaxed);
    }

    [[nodiscard]] size_t Available() const noexcept {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        const size_t head = m_head.load(std::memory_order_acquire);
        return (head >= tail) ? (head - tail) : 0;
    }

private:
    alignas(64) std::atomic<size_t> m_head{ 0 };
    alignas(64) std::atomic<size_t> m_tail{ 0 };
    alignas(64) T m_buffer[Capacity];
};

class AtomicConfigStore {
public:
    void Store(const TransformConfig& newCfg) noexcept {
        uint32_t s = m_seq.load(std::memory_order_relaxed);
        m_seq.store(s + 1, std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_release);
        m_cfg = newCfg;
        std::atomic_thread_fence(std::memory_order_release);
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
            result = m_cfg;
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

struct LatencyStats {
    static constexpr size_t HIST_CAP = 512;
    std::atomic<uint64_t> count{ 0 };
    std::atomic<uint32_t> lastUs{ 0 };
    std::atomic<uint32_t> maxUs{ 0 };
    std::atomic<uint32_t> p50Us{ 0 };
    std::atomic<uint32_t> p99Us{ 0 };
    std::atomic<uint32_t> minUs{ 0 };
    std::atomic<uint32_t> writeIdx{ 0 };
    uint32_t samples[HIST_CAP]{ 0 };

    inline void Record(uint64_t us) noexcept {
        uint32_t u = static_cast<uint32_t>(us);
        lastUs.store(u, std::memory_order_relaxed);
        count.fetch_add(1, std::memory_order_relaxed);

        uint32_t curMax = maxUs.load(std::memory_order_relaxed);
        while (u > curMax && !maxUs.compare_exchange_weak(curMax, u, std::memory_order_relaxed)) {}

        uint32_t idx = writeIdx.fetch_add(1, std::memory_order_relaxed);
        samples[idx & (HIST_CAP - 1)] = u;
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
            sorted[i] = samples[(startIdx + i) & (HIST_CAP - 1)];
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
    uint32_t intervals[HIST_CAP]{ 0 };
    uint32_t lastIntervalUs{ 0 };
    double rfcJitter{ 0.0 };

    inline void RecordInterval(uint64_t intvUs) noexcept {
        uint32_t u = static_cast<uint32_t>(intvUs);
        if (lastIntervalUs > 0) {
            double d = std::abs(static_cast<double>(u) - static_cast<double>(lastIntervalUs));
            rfcJitter += (d - rfcJitter) / 16.0;
            jitterUs.store(static_cast<uint32_t>(rfcJitter + 0.5), std::memory_order_relaxed);
        }
        lastIntervalUs = u;
        uint32_t idx = writeIdx.fetch_add(1, std::memory_order_relaxed);
        intervals[idx & (HIST_CAP - 1)] = u;
        count.fetch_add(1, std::memory_order_relaxed);
    }

    [[nodiscard]] inline uint32_t GetJitterUs() const noexcept {
        return jitterUs.load(std::memory_order_relaxed);
    }

    void Compute(uint32_t& out_stdDevUs, uint32_t& out_avgIntvUs) noexcept {
        uint32_t total = writeIdx.load(std::memory_order_relaxed);
        if (total < 2) {
            out_stdDevUs = 0;
            out_avgIntvUs = (total == 1) ? intervals[0] : 0;
            return;
        }

        uint32_t n = (std::min)(total, static_cast<uint32_t>(HIST_CAP));
        uint32_t startIdx = total - n;
        double sum = 0.0;
        for (uint32_t i = 0; i < n; ++i) {
            sum += intervals[(startIdx + i) & (HIST_CAP - 1)];
        }
        double mean = sum / n;
        double varSum = 0.0;
        for (uint32_t i = 0; i < n; ++i) {
            double diff = intervals[(startIdx + i) & (HIST_CAP - 1)] - mean;
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

        const double vx = (inX - m_lastX) / dt_ms;
        const double vy = (inY - m_lastY) / dt_ms;
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

        const double tau = (std::clamp)(cfg.tau_ms, 1.0, 5.0);

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
        const double ax = (vx - m_lastVx) / dt_ms;
        const double ay = (vy - m_lastVy) / dt_ms;
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

            // Ensure lead direction does not oppose current velocity
            if ((leadX * vx + leadY * vy) <= 0.0) {
                leadX = 0.0;
                leadY = 0.0;
            } else {
                const double curSpeed = std::sqrt(curSpeedSq);
                const double maxLead = 1.5 * curSpeed * tau;
                const double leadMag = std::sqrt(leadX * leadX + leadY * leadY);
                if (leadMag > maxLead && leadMag > 1e-6) {
                    const double scale = maxLead / leadMag;
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
            dt_ms = static_cast<double>(nowQpc - m_lastQpc) * 1000.0 / static_cast<double>(qpcFreq);
        }
        m_lastQpc = nowQpc;
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
extern AtomicConfigStore g_cfgStore;
extern std::atomic<bool> g_penClick;
extern std::atomic<bool> g_barrelClick;
extern bool g_clipArea;
extern bool g_calibrate;
extern bool g_extrapolation;
extern double g_extrapolationTau;
extern int g_spinPercent;
extern std::string g_dumpReportsFile;
extern std::atomic<bool> g_sharedModeFallback;

// --- Diagnostics & Performance Stats ---
struct CompactPenSample {
    int32_t raw_x = 0;
    int32_t raw_y = 0;
    int32_t px = 0;
    int32_t py = 0;
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

