#pragma once

#include <cstdint>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cstdlib>
#include <cstring>

#include "osupoint/ipc_protocol.h"
#include "osupoint/tablet_spec.h"

namespace osupoint {

constexpr double PI = 3.14159265358979323846;

struct MonitorRect {
    int left = 0;
    int top = 0;
    int right = 1920;
    int bottom = 1080;
    char name[32] = "Screen";
    bool is_primary = false;

    constexpr int Width() const noexcept { return right - left; }
    constexpr int Height() const noexcept { return bottom - top; }
};

struct VirtualDesktop {
    double x = 0.0;
    double y = 0.0;
    double width = 1920.0;
    double height = 1080.0;
};

struct DisplayGeometry {
    static constexpr size_t MAX_MONITORS = 16;
    MonitorRect monitors[MAX_MONITORS];
    size_t monitor_count = 1;
    VirtualDesktop virtual_desktop{0.0, 0.0, 1920.0, 1080.0};
    int32_t primary_idx = 0;
};

struct AreaConfig {
    double width_mm = 152.0;
    double height_mm = 95.0;
    double cx_mm = 76.0;
    double cy_mm = 47.5;
    double rotation_deg = 0.0;
    int32_t monitor_idx = 0;
    int32_t display_x = -1;
    int32_t display_y = -1;
    int32_t display_w = 0;
    int32_t display_h = 0;
    bool clip_area = false;
    bool unlock_limits = false;
};

struct Point2D {
    double x = 0.0;
    double y = 0.0;
};

struct RotatedCorners {
    Point2D corners_mm[4];    // TL, TR, BR, BL in mm
    Point2D corners_raw[4];   // TL, TR, BR, BL in raw tablet counts
    Point2D aabb_min_mm;
    Point2D aabb_max_mm;
};

// --- Mathematical Pipeline Functions ---

double ParseAspectRatio(std::string_view str) noexcept;
inline double ParseAspectRatio(const std::string& str) noexcept {
    return ParseAspectRatio(std::string_view(str));
}
inline double ParseAspectRatio(const char* str) noexcept {
    return ParseAspectRatio(str ? std::string_view(str) : std::string_view{});
}

std::string MatchMonitorAspectRatio(int width, int height, double& out_ratio) noexcept;

void ComputeRotatedCorners(double w_mm, double h_mm, double cx_mm, double cy_mm,
                           double rot_deg, double phys_w, double phys_h,
                           int32_t max_x, int32_t max_y,
                           RotatedCorners& out_corners) noexcept;

void ClampAreaToBounds(double& w_mm, double& h_mm, double& cx_mm, double& cy_mm,
                       double rot_deg, double phys_w, double phys_h,
                       bool lock_ratio = false, double ratio = 16.0 / 9.0) noexcept;

template <typename TabletType>
bool ComputeTransform(const TabletType& spec, const AreaConfig& area,
                      const MonitorRect& mon, const VirtualDesktop& virt,
                      TransformConfig& out_cfg) noexcept {
    if (spec.max_x <= 0 || spec.max_y <= 0 || spec.phys_w <= 0.0 || spec.phys_h <= 0.0) {
        return false;
    }
    if (!std::isfinite(spec.phys_w) || !std::isfinite(spec.phys_h) ||
        !std::isfinite(virt.width) || !std::isfinite(virt.height) ||
        !std::isfinite(area.rotation_deg) || !std::isfinite(area.cx_mm) || !std::isfinite(area.cy_mm)) return false;

    out_cfg.width_mm = static_cast<float>(area.width_mm);
    out_cfg.height_mm = static_cast<float>(area.height_mm);
    out_cfg.cx = static_cast<float>(area.cx_mm);
    out_cfg.cy = static_cast<float>(area.cy_mm);
    out_cfg.rotation_deg = static_cast<float>(area.rotation_deg);
    out_cfg.clip_area = area.clip_area ? 1 : 0;
    out_cfg.monitor_idx = area.monitor_idx;

    const double inv_scale_x = spec.phys_w / static_cast<double>(spec.max_x);
    const double inv_scale_y = spec.phys_h / static_cast<double>(spec.max_y);

    const double canon_deg = std::remainder(area.rotation_deg, 360.0);
    const double rad = (-canon_deg) * (PI / 180.0);
    const double cos_val = std::cos(rad);
    const double sin_val = std::sin(rad);

    const double w = (area.width_mm > 0.0) ? area.width_mm : 152.0;
    const double h = (area.height_mm > 0.0) ? area.height_mm : 95.0;

    const double a = cos_val / w;
    const double b = -sin_val / w;
    const double c = sin_val / h;
    const double d = cos_val / h;

    const int mon_w = mon.Width() > 0 ? mon.Width() : 1920;
    const int mon_h = mon.Height() > 0 ? mon.Height() : 1080;

    double eff_w = (area.display_w > 0) ? static_cast<double>(area.display_w) : static_cast<double>(mon_w);
    double eff_h = (area.display_h > 0) ? static_cast<double>(area.display_h) : static_cast<double>(mon_h);

    eff_w = std::min(static_cast<double>(mon_w), std::max(2.0, eff_w));
    eff_h = std::min(static_cast<double>(mon_h), std::max(2.0, eff_h));

    double off_x = (area.display_x >= 0) ? static_cast<double>(area.display_x) : (static_cast<double>(mon_w) - eff_w) * 0.5;
    double off_y = (area.display_y >= 0) ? static_cast<double>(area.display_y) : (static_cast<double>(mon_h) - eff_h) * 0.5;

    double target_l = static_cast<double>(mon.left) + off_x;
    double target_t = static_cast<double>(mon.top) + off_y;

    double denom_x = (virt.width > 1.0) ? (virt.width - 1.0) : 1.0;
    double denom_y = (virt.height > 1.0) ? (virt.height - 1.0) : 1.0;

    double scale_x = (eff_w - 1.0) * 65535.0 / denom_x;
    double shift_x = (target_l - virt.x) * 65535.0 / denom_x;

    double scale_y = (eff_h - 1.0) * 65535.0 / denom_y;
    double shift_y = (target_t - virt.y) * 65535.0 / denom_y;

    out_cfg.knx[0] = inv_scale_x * a;
    out_cfg.kny[0] = inv_scale_y * b;
    out_cfg.knc[0] = 0.5 - (area.cx_mm * a + area.cy_mm * b);

    out_cfg.knx[1] = inv_scale_x * c;
    out_cfg.kny[1] = inv_scale_y * d;
    out_cfg.knc[1] = 0.5 - (area.cx_mm * c + area.cy_mm * d);

    out_cfg.vscale[0] = scale_x;
    out_cfg.vscale[1] = scale_y;
    out_cfg.vshift[0] = shift_x + 0.5;
    out_cfg.vshift[1] = shift_y + 0.5;

    out_cfg.mx[0] = out_cfg.knx[0] * out_cfg.vscale[0];
    out_cfg.mx[1] = out_cfg.knx[1] * out_cfg.vscale[1];
    out_cfg.my[0] = out_cfg.kny[0] * out_cfg.vscale[0];
    out_cfg.my[1] = out_cfg.kny[1] * out_cfg.vscale[1];
    out_cfg.mc[0] = out_cfg.knc[0] * out_cfg.vscale[0] + out_cfg.vshift[0];
    out_cfg.mc[1] = out_cfg.knc[1] * out_cfg.vscale[1] + out_cfg.vshift[1];
    // The output is already in normalized desktop space. Advertise a physical
    // aspect matching that desktop, with nonzero resolutions required by libinput.
    const int desktop_w = static_cast<int>(std::clamp(virt.width, 1.0, 100000.0));
    const int desktop_h = static_cast<int>(std::clamp(virt.height, 1.0, 100000.0));
    const int divisor = std::gcd(desktop_w, desktop_h);
    const int rx = desktop_h / divisor, ry = desktop_w / divisor;
    const int unit = std::max(1, 1000 / std::max(rx, ry));
    out_cfg.output_resolution_x = rx * unit;
    out_cfg.output_resolution_y = ry * unit;

    return true;
}

// Hot-path runtime transformation: 2 scalar FMAs, zero division, zero heap allocations
inline void ApplyTransform(int32_t raw_x, int32_t raw_y, const TransformConfig& cfg,
                           double& out_x, double& out_y) noexcept {
    out_x = static_cast<double>(raw_x) * cfg.mx[0] + static_cast<double>(raw_y) * cfg.my[0] + cfg.mc[0];
    out_y = static_cast<double>(raw_x) * cfg.mx[1] + static_cast<double>(raw_y) * cfg.my[1] + cfg.mc[1];

    if (cfg.clip_area) {
        out_x = std::clamp(out_x, 0.0, 65535.0);
        out_y = std::clamp(out_y, 0.0, 65535.0);
    }
}

// Stateful helper class preserving 100% test suite compatibility with test_math.cpp
class CoordinateTransformer {
public:
    template <typename TabletType>
    void Configure(const TabletType& spec,
                   double w, double h, double cx, double cy, double rot_deg,
                   int disp_x, int disp_y, int disp_w, int disp_h,
                   const MonitorRect& rcMon, const VirtualDesktop& virt,
                   bool clip_area = false) noexcept {
        AreaConfig area;
        area.width_mm = w;
        area.height_mm = h;
        area.cx_mm = cx;
        area.cy_mm = cy;
        area.rotation_deg = rot_deg;
        area.display_x = disp_x;
        area.display_y = disp_y;
        area.display_w = disp_w;
        area.display_h = disp_h;
        area.clip_area = clip_area;

        m_mon = rcMon;
        m_virt = virt;
        ComputeTransform(spec, area, rcMon, virt, m_cfg);
    }

    void TransformRaw(int raw_x, int raw_y, double& un, double& vn, int& px, int& py) const noexcept {
        double vfx = raw_x;
        double vfy = raw_y;

        un = vfx * m_cfg.knx[0] + vfy * m_cfg.kny[0] + m_cfg.knc[0];
        vn = vfx * m_cfg.knx[1] + vfy * m_cfg.kny[1] + m_cfg.knc[1];

        double out_x = un * m_cfg.vscale[0] + m_cfg.vshift[0];
        double out_y = vn * m_cfg.vscale[1] + m_cfg.vshift[1];

        if (m_cfg.clip_area) {
            out_x = std::clamp(out_x, 0.0, 65535.0);
            out_y = std::clamp(out_y, 0.0, 65535.0);
        }

        double denom_x = (m_virt.width > 1.0) ? (m_virt.width - 1.0) : 1.0;
        double denom_y = (m_virt.height > 1.0) ? (m_virt.height - 1.0) : 1.0;

        px = static_cast<int>((out_x * denom_x) / 65535.0 + m_virt.x);
        py = static_cast<int>((out_y * denom_y) / 65535.0 + m_virt.y);
    }

    void TransformRawFast(int raw_x, int raw_y, int& px, int& py) const noexcept {
        double out_x = 0.0, out_y = 0.0;
        ApplyTransform(raw_x, raw_y, m_cfg, out_x, out_y);

        double denom_x = (m_virt.width > 1.0) ? (m_virt.width - 1.0) : 1.0;
        double denom_y = (m_virt.height > 1.0) ? (m_virt.height - 1.0) : 1.0;

        px = static_cast<int>((out_x * denom_x) / 65535.0 + m_virt.x);
        py = static_cast<int>((out_y * denom_y) / 65535.0 + m_virt.y);
    }

    const TransformConfig& Config() const noexcept { return m_cfg; }

private:
    TransformConfig m_cfg;
    MonitorRect m_mon;
    VirtualDesktop m_virt;
};

// Telemetry & Statistics Classes
class LatencyStats {
public:
    void Record(uint32_t latencyUs) { m_samples.push_back(latencyUs); }
    void Reset() { m_samples.clear(); }
    void Compute(uint32_t& p50, uint32_t& p99, uint32_t& maxUs, uint32_t& minUs) const {
        if (m_samples.empty()) {
            p50 = p99 = maxUs = minUs = 0;
            return;
        }
        std::vector<uint32_t> sorted = m_samples;
        std::sort(sorted.begin(), sorted.end());
        minUs = sorted.front();
        maxUs = sorted.back();
        size_t idx50 = sorted.size() * 50 / 100;
        if (idx50 >= sorted.size()) idx50 = sorted.size() - 1;
        p50 = sorted[idx50];
        size_t idx99 = sorted.size() * 99 / 100;
        if (idx99 >= sorted.size()) idx99 = sorted.size() - 1;
        p99 = sorted[idx99];
    }
private:
    std::vector<uint32_t> m_samples;
};

class JitterStats {
public:
    void RecordInterval(uint32_t intervalUs) { m_intervals.push_back(intervalUs); }
    void Reset() { m_intervals.clear(); }
    uint32_t GetJitterUs() const {
        uint32_t stdDev = 0, avg = 0;
        Compute(stdDev, avg);
        return stdDev;
    }
    void Compute(uint32_t& stdDev, uint32_t& avgIntv) const {
        if (m_intervals.size() < 2) {
            stdDev = 0;
            avgIntv = m_intervals.empty() ? 0 : m_intervals[0];
            return;
        }
        double sum = 0.0;
        for (auto v : m_intervals) sum += v;
        double mean = sum / static_cast<double>(m_intervals.size());
        avgIntv = static_cast<uint32_t>(std::round(mean));
        double varSum = 0.0;
        for (auto v : m_intervals) {
            double diff = static_cast<double>(v) - mean;
            varSum += diff * diff;
        }
        double variance = varSum / static_cast<double>(m_intervals.size() - 1);
        stdDev = static_cast<uint32_t>(std::round(std::sqrt(variance)));
    }
private:
    std::vector<uint32_t> m_intervals;
};

} // namespace osupoint

// Expose in global scope for test runner compatibility
using osupoint::CoordinateTransformer;
using osupoint::LatencyStats;
using osupoint::JitterStats;
using osupoint::MonitorRect;
using osupoint::VirtualDesktop;
using osupoint::DisplayGeometry;
using osupoint::AreaConfig;
using osupoint::Point2D;
using osupoint::RotatedCorners;
using osupoint::ParseAspectRatio;
using osupoint::ApplyTransform;
using osupoint::ComputeTransform;
using osupoint::ComputeRotatedCorners;
using osupoint::ClampAreaToBounds;
using osupoint::MatchMonitorAspectRatio;
