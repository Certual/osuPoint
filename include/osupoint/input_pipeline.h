#pragma once

#include "osupoint/motion_filters.h"
#include "osupoint/transform_pipeline.h"

namespace osupoint {

struct InputOutput {
    int32_t x = 0, y = 0;
    uint16_t pressure = 0;
    bool in_range = false, left = false, right = false;
};

inline bool ValidTransformConfig(const TransformConfig& cfg) noexcept {
    for (const double* pair : {cfg.mx, cfg.my, cfg.mc, cfg.knx, cfg.kny, cfg.knc, cfg.vscale, cfg.vshift})
        if (!std::isfinite(pair[0]) || !std::isfinite(pair[1]) || std::abs(pair[0]) > 1e12 || std::abs(pair[1]) > 1e12) return false;
    return std::isfinite(cfg.width_mm) && cfg.width_mm > 0 &&
           std::isfinite(cfg.height_mm) && cfg.height_mm > 0 &&
           std::isfinite(cfg.cx) && std::isfinite(cfg.cy) && std::isfinite(cfg.rotation_deg) &&
           std::isfinite(cfg.extrapolation_tau) && cfg.extrapolation_tau >= 0 && cfg.extrapolation_tau <= 50.0f &&
           cfg.filter_1euro <= 100 && cfg.filter_motion <= 100 && cfg.pressure_max > 0 && cfg.spin_percent >= 0 && cfg.spin_percent <= 200 &&
           cfg.output_resolution_x > 0 && cfg.output_resolution_y > 0 && cfg.output_resolution_x <= 100000 && cfg.output_resolution_y <= 100000 &&
           cfg.mouse_mode <= 1 && cfg.debug_mode <= 1 && cfg.clip_area <= 1 && cfg.extrapolation <= 1 && cfg.pen_click <= 1 && cfg.barrel_btn <= 1;
}

inline void RebindTabletTransform(const RuntimeTabletSpec& spec, TransformConfig& cfg) noexcept {
    const double sx = cfg.vscale[0], sy = cfg.vscale[1], tx = cfg.vshift[0], ty = cfg.vshift[1];
    const int rx = cfg.output_resolution_x, ry = cfg.output_resolution_y;
    AreaConfig area;
    area.width_mm = cfg.width_mm; area.height_mm = cfg.height_mm;
    area.cx_mm = cfg.cx; area.cy_mm = cfg.cy; area.rotation_deg = cfg.rotation_deg;
    area.clip_area = cfg.clip_area; area.monitor_idx = cfg.monitor_idx;
    if (ComputeTransform(spec, area, MonitorRect{}, VirtualDesktop{}, cfg)) {
        cfg.vscale[0] = sx; cfg.vscale[1] = sy; cfg.vshift[0] = tx; cfg.vshift[1] = ty;
        cfg.output_resolution_x = rx; cfg.output_resolution_y = ry;
        cfg.mx[0] = cfg.knx[0] * sx; cfg.mx[1] = cfg.knx[1] * sy;
        cfg.my[0] = cfg.kny[0] * sx; cfg.my[1] = cfg.kny[1] * sy;
        cfg.mc[0] = cfg.knc[0] * sx + tx; cfg.mc[1] = cfg.knc[1] * sy + ty;
    }
}

// This is the actual daemon pipeline, also exercised by the production tests.
// Filter in tablet millimetres BEFORE display mapping: tuning is independent of
// monitor resolution, active area and the tablet's raw coordinate resolution.
class InputPipeline {
public:
    void Configure(const RuntimeTabletSpec& spec, const TransformConfig& cfg) noexcept {
        const double sx = spec.max_x > 0 && spec.phys_w > 0 ? spec.phys_w / spec.max_x : 0.01;
        const double sy = spec.max_y > 0 && spec.phys_h > 0 ? spec.phys_h / spec.max_y : 0.01;
        if (sx != m_mm_x || sy != m_mm_y || cfg.mouse_mode != m_cfg.mouse_mode) Reset();
        m_mm_x = sx; m_mm_y = sy;
        m_counts_x = 1.0 / sx; m_counts_y = 1.0 / sy;
        m_filters.UpdateConfig(cfg);
        m_cfg = cfg;
    }
    bool FiltersEnabled() const noexcept {
        return m_cfg.filter_1euro || m_cfg.filter_motion || m_cfg.extrapolation;
    }
    void Reset() noexcept { m_filters.Reset(); m_has_output = false; m_last = {}; }
    bool Process(const PenSample& sample, double dt, InputOutput& out, DebugPoint* debug_point = nullptr) noexcept {
        out.in_range = (sample.flags & PEN_FLAG_IN_RANGE) != 0;
        out.pressure = std::min(sample.pressure, m_cfg.pressure_max);
        const uint16_t threshold = std::max<uint16_t>(1, m_cfg.tip_pressure_threshold);
        const bool pressed = (sample.flags & PEN_FLAG_RAW_HW_TIP) ? sample.pressure >= threshold
            : (sample.flags & PEN_FLAG_PRESSED) != 0;
        out.left = out.in_range && pressed && m_cfg.pen_click;
        out.right = out.in_range && (sample.flags & PEN_FLAG_BARREL) && m_cfg.barrel_btn;
        double raw_x = sample.raw_x, raw_y = sample.raw_y;
        double mm_x = 0, mm_y = 0, filtered_x = 0, filtered_y = 0;
        if (FiltersEnabled() || debug_point) {
            mm_x = raw_x * m_mm_x; mm_y = raw_y * m_mm_y;
            filtered_x = mm_x; filtered_y = mm_y;
            if (out.in_range && FiltersEnabled()) {
                m_filters.Process(mm_x, mm_y, dt, filtered_x, filtered_y);
                raw_x = filtered_x * m_counts_x; raw_y = filtered_y * m_counts_y;
            }
        }
        if (!out.in_range) m_filters.Reset();
        if (debug_point) *debug_point = {static_cast<float>(mm_x), static_cast<float>(mm_y),
                                        static_cast<float>(filtered_x), static_cast<float>(filtered_y)};
        double x, y;
        if (m_cfg.mx[0] != 0 || m_cfg.my[0] != 0) {
            if (m_cfg.clip_area) {
                double nx = raw_x * m_cfg.knx[0] + raw_y * m_cfg.kny[0] + m_cfg.knc[0];
                double ny = raw_x * m_cfg.knx[1] + raw_y * m_cfg.kny[1] + m_cfg.knc[1];
                nx = std::clamp(nx, 0.0, 1.0);
                ny = std::clamp(ny, 0.0, 1.0);
                x = nx * m_cfg.vscale[0] + m_cfg.vshift[0];
                y = ny * m_cfg.vscale[1] + m_cfg.vshift[1];
            } else {
                x = raw_x * m_cfg.mx[0] + raw_y * m_cfg.my[0] + m_cfg.mc[0];
                y = raw_x * m_cfg.mx[1] + raw_y * m_cfg.my[1] + m_cfg.mc[1];
            }
        } else {
            x = raw_x * m_mm_x * (65535.0 / 152.0);
            y = raw_y * m_mm_y * (65535.0 / 95.0);
        }
        if (!std::isfinite(x) || !std::isfinite(y)) return false;
        out.x = static_cast<int32_t>(std::clamp(x + 0.5, 0.0, 65535.0));
        out.y = static_cast<int32_t>(std::clamp(y + 0.5, 0.0, 65535.0));
        return true;
    }
    InputOutput Lift() noexcept {
        m_filters.Reset();
        InputOutput out = m_last;
        out.pressure = 0; out.in_range = out.left = out.right = false;
        return out;
    }
    [[nodiscard]] const InputOutput& Last() const noexcept { return m_last; }
    bool ShouldEmit(const InputOutput& out) const noexcept {
        return !m_has_output || out.x != m_last.x || out.y != m_last.y ||
               out.left != m_last.left || out.right != m_last.right ||
               out.in_range != m_last.in_range ||
               (!m_cfg.mouse_mode && out.pressure != m_last.pressure);
    }
    // Commit only after a complete SYN_REPORT write. Failed output is retried.
    void Commit(const InputOutput& out) noexcept { m_last = out; m_has_output = true; }
private:
    TransformConfig m_cfg{};
    FilterCascade m_filters;
    InputOutput m_last{};
    double m_mm_x = 0.01, m_mm_y = 0.01, m_counts_x = 100, m_counts_y = 100;
    bool m_has_output = false;
};

} // namespace osupoint
