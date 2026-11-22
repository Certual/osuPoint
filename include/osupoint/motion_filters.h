#pragma once

#include "osupoint/ipc_protocol.h"
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace osupoint {

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

    [[nodiscard]] double Value() const noexcept { return m_hatX; }
    [[nodiscard]] bool HasValue() const noexcept { return m_hasValue; }

private:
    double m_hatX = 0.0;
    bool m_hasValue = false;
};

class OneEuroFilter {
public:
    explicit OneEuroFilter(double minCutoff = 1.0, double beta = 0.007, double dCutoff = 1.0) noexcept
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
        // Coordinates are physical millimetres: smooth hand jitter at rest,
        // open the cutoff quickly for deliberate movement.
        double minCutoff = 18.0 - pct * 15.0;
        double beta = 0.20 + (1.0 - pct) * 0.80;
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

    [[nodiscard]] bool IsEnabled() const noexcept { return m_enabled; }

private:
    bool m_enabled = true;
    OneEuroFilter m_fx;
    OneEuroFilter m_fy;
};

class SoftKneeMotionFilter {
public:
    explicit SoftKneeMotionFilter(int strengthPercent = 40, double vStart = 15.0, double vFull = 65.0) noexcept
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

    void SetCustomParams(double user_cutoff, double user_v_start, double user_v_full) noexcept {
        if (user_cutoff > 0.0) m_cutoffHz = user_cutoff;
        if (user_v_start > 0.0) m_vStart = user_v_start;
        if (user_v_full > 0.0) m_vFull = user_v_full;
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

        double t = std::clamp((speed - m_vStart) / (m_vFull - m_vStart), 0.0, 1.0);
        double s = t * t * (3.0 - 2.0 * t);

        double alpha = OneEuroFilter::Alpha(m_cutoffHz, dt_sec);
        double filteredX = m_lpX.Filter(inX, alpha);
        double filteredY = m_lpY.Filter(inY, alpha);

        outX = (1.0 - s) * filteredX + s * inX;
        outY = (1.0 - s) * filteredY + s * inY;
    }

    [[nodiscard]] double CutoffHz() const noexcept { return m_cutoffHz; }
    [[nodiscard]] double VStart() const noexcept { return m_vStart; }
    [[nodiscard]] double VFull() const noexcept { return m_vFull; }

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

        constexpr double NOISE_SPEED_SQ = 0.000025; // physical speed < 5 mm/s
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

        const double tau = std::clamp(cfg.tau_ms, 0.0, 5.0);

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

class FilterCascade {
public:
    void Reset() noexcept {
        m_oneEuro.Reset();
        m_softKnee.Reset();
        m_extrapolator.Reset();
    }

    void UpdateConfig(const TransformConfig& cfg) noexcept {
        if (m_strengthEuro != cfg.filter_1euro || m_strengthMotion != cfg.filter_motion ||
            m_extraCfg.enabled != (cfg.extrapolation != 0) || m_extraCfg.tau_ms != cfg.extrapolation_tau) {
            Reset();
        }
        m_strengthEuro = cfg.filter_1euro;
        m_strengthMotion = cfg.filter_motion;
        m_oneEuro.Configure(cfg.filter_1euro);
        m_softKnee.SetStrength(cfg.filter_motion);
        if (cfg.user_cutoff > 0.0f || cfg.user_v_start > 0.0f || cfg.user_v_full > 0.0f) {
            m_softKnee.SetCustomParams(cfg.user_cutoff, cfg.user_v_start, cfg.user_v_full);
        }
        m_extraCfg.enabled = (cfg.extrapolation != 0);
        m_extraCfg.tau_ms = cfg.extrapolation_tau;
    }

    void Process(double inX, double inY, double dt_sec, double& outX, double& outY) noexcept {
        if (!std::isfinite(dt_sec) || dt_sec <= 0.0 || dt_sec > 0.040) {
            Reset();
            outX = inX;
            outY = inY;
            return;
        }
        const double eff_dt = std::max(dt_sec, 0.0001);
        double s1X = inX, s1Y = inY;
        m_oneEuro.Filter(inX, inY, eff_dt, s1X, s1Y);

        double s2X = s1X, s2Y = s1Y;
        m_softKnee.Filter(s1X, s1Y, eff_dt, s2X, s2Y);

        double s3X = s2X, s3Y = s2Y;
        m_extrapolator.UpdateWithDt(s2X, s2Y, eff_dt * 1000.0, m_extraCfg, s3X, s3Y);

        outX = s3X;
        outY = s3Y;
    }

    [[nodiscard]] const KinematicExtrapolator& Extrapolator() const noexcept { return m_extrapolator; }
    [[nodiscard]] const SoftKneeMotionFilter& SoftKnee() const noexcept { return m_softKnee; }
    [[nodiscard]] const OneEuroFilter2D& OneEuro() const noexcept { return m_oneEuro; }

private:
    OneEuroFilter2D m_oneEuro;
    SoftKneeMotionFilter m_softKnee;
    KinematicExtrapolator m_extrapolator;
    ExtrapolationConfig m_extraCfg;
    int m_strengthEuro = -1, m_strengthMotion = -1;
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
    double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

inline double ComputeAlpha(double dt, double cutoff_hz, double factor = 6.283185307179586) noexcept {
    double safe_dt = (dt < 0.0001) ? 0.0001 : dt;
    double tau = factor * cutoff_hz * safe_dt;
    if (std::isnan(tau) || tau > 1e12) return 1.0;
    double alpha = tau / (1.0 + tau);
    return std::clamp(alpha, 0.0, 1.0);
}

class AdaptiveFilterPipeline {
public:
    void Reset() noexcept {
        m_initialized = false;
        m_prevX = 0.0;
        m_prevY = 0.0;
        m_prevRawX = 0.0;
        m_prevRawY = 0.0;
        m_prevDx = 0.0;
        m_prevDy = 0.0;
        m_filteredDx = 0.0;
        m_filteredDy = 0.0;
        m_warmupTicks = 10;
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
            m_filteredDx = 0.0;
            m_filteredDy = 0.0;
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

        // Turnaround check
        const double dot = dx * m_prevDx + dy * m_prevDy;
        m_prevDx = dx;
        m_prevDy = dy;

        if (extrapolation_mm > 0.0 && speed > 1e-4 && dot >= 0.0) {
            double lead_factor = extrapolation_mm / speed;
            outX = x_filt + m_filteredDx * lead_factor;
            outY = y_filt + m_filteredDy * lead_factor;
        } else {
            outX = x_filt;
            outY = y_filt;
        }
    }
private:
    bool   m_initialized = false;
    double m_prevX = 0.0, m_prevY = 0.0;
    double m_prevRawX = 0.0, m_prevRawY = 0.0;
    double m_prevDx = 0.0, m_prevDy = 0.0;
    double m_filteredDx = 0.0, m_filteredDy = 0.0;
    int    m_warmupTicks = 10;
};

} // namespace osupoint

// Expose in global scope for test runner compatibility
using osupoint::LowPassFilter;
using osupoint::OneEuroFilter;
using osupoint::OneEuroFilter2D;
using osupoint::SoftKneeMotionFilter;
using osupoint::ExtrapolationConfig;
using osupoint::KinematicExtrapolator;
using osupoint::FilterCascade;
using osupoint::AdaptiveFilterPipeline;
using osupoint::FilterParams;
using osupoint::ComputeFilterParams;
