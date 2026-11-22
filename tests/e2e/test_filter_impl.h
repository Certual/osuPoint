#pragma once

#include <cmath>
#include <algorithm>
#include <cstdint>

// --- 1-Euro Adaptive Filter ---

class LowPassFilter {
public:
    LowPassFilter() = default;

    void Reset() noexcept {
        m_hasValue = false;
        m_hatX = 0.0;
    }

    double Filter(double x, double alpha) noexcept {
        if (!m_hasValue) {
            m_hatX = x;
            m_hasValue = true;
            return x;
        }
        m_hatX = alpha * x + (1.0 - alpha) * m_hatX;
        return m_hatX;
    }

    double Value() const noexcept { return m_hatX; }
    bool HasValue() const noexcept { return m_hasValue; }

private:
    double m_hatX = 0.0;
    bool m_hasValue = false;
};

class OneEuroFilter {
public:
    OneEuroFilter(double minCutoff = 1.0, double beta = 0.007, double dCutoff = 1.0) noexcept
        : m_minCutoff(minCutoff), m_beta(beta), m_dCutoff(dCutoff) {}

    void Reset() noexcept {
        m_xFilter.Reset();
        m_dxFilter.Reset();
        m_hasLast = false;
        m_lastX = 0.0;
    }

    void SetParameters(double minCutoff, double beta, double dCutoff) noexcept {
        m_minCutoff = minCutoff;
        m_beta = beta;
        m_dCutoff = dCutoff;
    }

    double Filter(double x, double dt_sec) noexcept {
        if (dt_sec <= 0.0) return x;

        if (!m_hasLast) {
            m_lastX = x;
            m_hasLast = true;
            return m_xFilter.Filter(x, 1.0);
        }

        double dx = (x - m_lastX) / dt_sec;
        m_lastX = x;

        double alphaD = Alpha(m_dCutoff, dt_sec);
        double edx = m_dxFilter.Filter(dx, alphaD);

        double cutoff = m_minCutoff + m_beta * std::abs(edx);
        double alphaX = Alpha(cutoff, dt_sec);

        return m_xFilter.Filter(x, alphaX);
    }

    static double Alpha(double cutoff, double dt) noexcept {
        constexpr double TWO_PI = 6.28318530717958647692;
        double tau = 1.0 / (TWO_PI * cutoff);
        return 1.0 / (1.0 + tau / dt);
    }

private:
    double m_minCutoff;
    double m_beta;
    double m_dCutoff;
    double m_lastX = 0.0;
    bool m_hasLast = false;
    LowPassFilter m_xFilter;
    LowPassFilter m_dxFilter;
};

class OneEuroFilter2D {
public:
    void Reset() noexcept {
        m_fx.Reset();
        m_fy.Reset();
    }

    void Configure(int percentStrength) noexcept {
        if (percentStrength <= 0) {
            m_enabled = false;
            return;
        }
        m_enabled = true;
        double pct = std::clamp(percentStrength, 1, 100) / 100.0;
        // At 1%: minCutoff ~ 5.0 Hz (light). At 100%: minCutoff ~ 0.5 Hz (heavy).
        double minCutoff = 5.0 - pct * 4.5;
        double beta = 0.001 + (1.0 - pct) * 0.01;
        m_fx.SetParameters(minCutoff, beta, 1.0);
        m_fy.SetParameters(minCutoff, beta, 1.0);
    }

    void Filter(double inX, double inY, double dt_sec, double& outX, double& outY) noexcept {
        if (!m_enabled || dt_sec <= 0.0) {
            outX = inX;
            outY = inY;
            return;
        }
        outX = m_fx.Filter(inX, dt_sec);
        outY = m_fy.Filter(inY, dt_sec);
    }

    bool IsEnabled() const noexcept { return m_enabled; }

private:
    bool m_enabled = true;
    OneEuroFilter m_fx;
    OneEuroFilter m_fy;
};

// --- Soft-Knee Quadratic / Hermite Cubic Smoothstep Motion Filter ---

class SoftKneeMotionFilter {
public:
    SoftKneeMotionFilter(int strengthPercent = 40, double vStart = 15.0, double vFull = 65.0) noexcept
        : m_strength(strengthPercent), m_vStart(vStart), m_vFull(vFull) {
        UpdateCutoff();
    }

    void Reset() noexcept {
        m_lpX.Reset();
        m_lpY.Reset();
        m_lastX = 0.0;
        m_lastY = 0.0;
        m_hasPos = false;
    }

    void SetStrength(int strengthPercent) noexcept {
        m_strength = strengthPercent;
        UpdateCutoff();
    }

    void Filter(double inX, double inY, double dt_sec, double& outX, double& outY) noexcept {
        if (m_strength <= 0 || dt_sec <= 0.0) {
            outX = inX;
            outY = inY;
            return;
        }

        if (!m_hasPos) {
            m_lastX = inX;
            m_lastY = inY;
            m_hasPos = true;
            m_lpX.Filter(inX, 1.0);
            m_lpY.Filter(inY, 1.0);
            outX = inX;
            outY = inY;
            return;
        }

        double vx = (inX - m_lastX) / dt_sec;
        double vy = (inY - m_lastY) / dt_sec;
        double speed = std::sqrt(vx * vx + vy * vy);
        m_lastX = inX;
        m_lastY = inY;

        // Smoothstep Hermite Soft-Knee transition
        double t = std::clamp((speed - m_vStart) / (m_vFull - m_vStart), 0.0, 1.0);
        double s = t * t * (3.0 - 2.0 * t); // S(t) = t^2 * (3 - 2t)

        double alpha = OneEuroFilter::Alpha(m_cutoffHz, dt_sec);
        double filteredX = m_lpX.Filter(inX, alpha);
        double filteredY = m_lpY.Filter(inY, alpha);

        // Blend: At rest (speed <= vStart), s = 0.0 -> fully filtered.
        // At high flick (speed >= vFull), s = 1.0 -> raw passthrough.
        outX = (1.0 - s) * filteredX + s * inX;
        outY = (1.0 - s) * filteredY + s * inY;
    }

    double CutoffHz() const noexcept { return m_cutoffHz; }
    double VStart() const noexcept { return m_vStart; }
    double VFull() const noexcept { return m_vFull; }

    static double ComputeSmoothstep(double v, double vStart, double vFull) noexcept {
        double t = std::clamp((v - vStart) / (vFull - vStart), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }

private:
    void UpdateCutoff() noexcept {
        if (m_strength <= 100) {
            m_cutoffHz = 70.0 - (static_cast<double>(m_strength) * 0.56);
        } else {
            m_cutoffHz = std::max(14.0 / std::sqrt(static_cast<double>(m_strength) * 0.01), 3.0);
        }
    }

    int m_strength;
    double m_vStart;
    double m_vFull;
    double m_cutoffHz = 47.6;
    double m_lastX = 0.0;
    double m_lastY = 0.0;
    bool m_hasPos = false;
    LowPassFilter m_lpX;
    LowPassFilter m_lpY;
};

// --- Adaptive Kinematic Extrapolator ---

struct ExtrapolationConfig {
    bool enabled = true;
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

        const double tau = std::clamp(cfg.tau_ms, 1.0, 5.0);

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

            const double leadDotVel = leadX * vx + leadY * vy;
            if (leadDotVel <= 0.0) {
                leadX = 0.0;
                leadY = 0.0;
            } else {
                const double maxLeadSq = (2.25 * tau * tau) * curSpeedSq;
                const double leadMagSq = leadX * leadX + leadY * leadY;
                if (leadMagSq > maxLeadSq && leadMagSq > 1e-12) {
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
