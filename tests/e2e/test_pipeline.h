#pragma once

#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <numeric>
#include "test_vectors.h"

constexpr double PI = 3.14159265358979323846;

struct alignas(64) TransformConfig {
    double mx[2] = {0.0, 0.0};
    double my[2] = {0.0, 0.0};
    double mc[2] = {0.0, 0.0};
    double knx[2] = {0.0, 0.0};
    double kny[2] = {0.0, 0.0};
    double knc[2] = {0.0, 0.0};
    double vscale[2] = {0.0, 0.0};
    double vshift[2] = {0.0, 0.0};
    float  width_mm = 152.0f;
    float  height_mm = 95.0f;
    float  cx = 76.0f;
    float  cy = 47.5f;
    float  rotation_deg = 0.0f;
    int32_t monitor_idx = 0;
    float  extrapolation_tau = 2.5f;
    int32_t spin_percent = 0;
    uint8_t clip_area = 0;
    uint8_t extrapolation = 1;
    uint8_t filter_1euro = 25;
    uint8_t filter_motion = 40;
    uint8_t mouse_mode = 0;
    uint8_t pen_click = 1;
    uint8_t barrel_btn = 1;
    uint8_t reserved[9] = {0};
};

struct MonitorRect {
    int left = 0;
    int top = 0;
    int right = 1920;
    int bottom = 1080;

    int Width() const noexcept { return right - left; }
    int Height() const noexcept { return bottom - top; }
};

struct VirtualDesktop {
    double x = 0.0;
    double y = 0.0;
    double width = 1920.0;
    double height = 1080.0;
};

inline double ParseAspectRatio(const std::string& str) {
    if (str.empty()) return 16.0 / 9.0;

    std::string s = str;
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return 16.0 / 9.0;
    size_t last = s.find_last_not_of(" \t\r\n");
    s = s.substr(first, (last - first + 1));

    size_t sep = s.find_first_of(":/xX,");
    if (sep != std::string::npos) {
        try {
            std::string sNum = s.substr(0, sep);
            std::string sDen = s.substr(sep + 1);
            while (!sNum.empty() && (sNum.front() == ' ' || sNum.front() == '\t')) sNum.erase(0, 1);
            while (!sNum.empty() && (sNum.back() == ' ' || sNum.back() == '\t')) sNum.pop_back();
            while (!sDen.empty() && (sDen.front() == ' ' || sDen.front() == '\t')) sDen.erase(0, 1);
            while (!sDen.empty() && (sDen.back() == ' ' || sDen.back() == '\t')) sDen.pop_back();

            if (!sNum.empty() && !sDen.empty()) {
                double num = std::stod(sNum);
                double den = std::stod(sDen);
                if (den > 0.0 && num > 0.0) {
                    return num / den;
                }
            }
        } catch (...) {}
    }

    try {
        double val = std::stod(s);
        if (val > 0.0) return val;
    } catch (...) {}

    return 16.0 / 9.0;
}

class CoordinateTransformer {
public:
    void Configure(const TabletSpec& spec,
                   double w, double h, double cx, double cy, double rot_deg,
                   int disp_x, int disp_y, int disp_w, int disp_h,
                   const MonitorRect& rcMon, const VirtualDesktop& virt,
                   bool clip_area = false) {
        m_spec = spec;
        m_cfg.width_mm = static_cast<float>(w);
        m_cfg.height_mm = static_cast<float>(h);
        m_cfg.cx = static_cast<float>(cx);
        m_cfg.cy = static_cast<float>(cy);
        m_cfg.rotation_deg = static_cast<float>(rot_deg);
        m_cfg.clip_area = clip_area ? 1 : 0;
        m_virt = virt;
        m_mon = rcMon;

        const double inv_scale_x = spec.phys_w / static_cast<double>(spec.max_x);
        const double inv_scale_y = spec.phys_h / static_cast<double>(spec.max_y);

        const double rad = (-rot_deg) * (PI / 180.0);
        const double cos_val = std::cos(rad);
        const double sin_val = std::sin(rad);

        const double a = cos_val / w;
        const double b = -sin_val / w;
        const double c = sin_val / h;
        const double d = cos_val / h;

        int mon_w = rcMon.Width();
        int mon_h = rcMon.Height();

        double eff_w = (disp_w > 0) ? static_cast<double>(disp_w) : static_cast<double>(mon_w);
        double eff_h = (disp_h > 0) ? static_cast<double>(disp_h) : static_cast<double>(mon_h);

        eff_w = std::min(static_cast<double>(mon_w), std::max(2.0, eff_w));
        eff_h = std::min(static_cast<double>(mon_h), std::max(2.0, eff_h));

        double off_x = (disp_x >= 0) ? static_cast<double>(disp_x) : (static_cast<double>(mon_w) - eff_w) * 0.5;
        double off_y = (disp_y >= 0) ? static_cast<double>(disp_y) : (static_cast<double>(mon_h) - eff_h) * 0.5;

        double target_l = static_cast<double>(rcMon.left) + off_x;
        double target_t = static_cast<double>(rcMon.top) + off_y;

        double denom_x = (virt.width > 1.0) ? (virt.width - 1.0) : 1.0;
        double denom_y = (virt.height > 1.0) ? (virt.height - 1.0) : 1.0;

        double scale_x = (eff_w - 1.0) * 65535.0 / denom_x;
        double shift_x = (target_l - virt.x) * 65535.0 / denom_x;

        double scale_y = (eff_h - 1.0) * 65535.0 / denom_y;
        double shift_y = (target_t - virt.y) * 65535.0 / denom_y;

        m_cfg.knx[0] = inv_scale_x * a;
        m_cfg.kny[0] = inv_scale_y * b;
        m_cfg.knc[0] = 0.5 - (cx * a + cy * b);

        m_cfg.knx[1] = inv_scale_x * c;
        m_cfg.kny[1] = inv_scale_y * d;
        m_cfg.knc[1] = 0.5 - (cx * c + cy * d);

        m_cfg.vscale[0] = scale_x;
        m_cfg.vscale[1] = scale_y;
        m_cfg.vshift[0] = shift_x + 0.5;
        m_cfg.vshift[1] = shift_y + 0.5;

        m_cfg.mx[0] = m_cfg.knx[0] * m_cfg.vscale[0];
        m_cfg.mx[1] = m_cfg.knx[1] * m_cfg.vscale[1];
        m_cfg.my[0] = m_cfg.kny[0] * m_cfg.vscale[0];
        m_cfg.my[1] = m_cfg.kny[1] * m_cfg.vscale[1];
        m_cfg.mc[0] = m_cfg.knc[0] * m_cfg.vscale[0] + m_cfg.vshift[0];
        m_cfg.mc[1] = m_cfg.knc[1] * m_cfg.vscale[1] + m_cfg.vshift[1];
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
        double out_x = raw_x * m_cfg.mx[0] + raw_y * m_cfg.my[0] + m_cfg.mc[0];
        double out_y = raw_x * m_cfg.mx[1] + raw_y * m_cfg.my[1] + m_cfg.mc[1];

        if (m_cfg.clip_area) {
            out_x = std::clamp(out_x, 0.0, 65535.0);
            out_y = std::clamp(out_y, 0.0, 65535.0);
        }

        double denom_x = (m_virt.width > 1.0) ? (m_virt.width - 1.0) : 1.0;
        double denom_y = (m_virt.height > 1.0) ? (m_virt.height - 1.0) : 1.0;

        px = static_cast<int>((out_x * denom_x) / 65535.0 + m_virt.x);
        py = static_cast<int>((out_y * denom_y) / 65535.0 + m_virt.y);
    }

    const TransformConfig& Config() const noexcept { return m_cfg; }

private:
    TabletSpec m_spec;
    TransformConfig m_cfg;
    MonitorRect m_mon;
    VirtualDesktop m_virt;
};

class LatencyStats {
public:
    void Record(uint32_t latencyUs) {
        m_samples.push_back(latencyUs);
    }

    void Reset() {
        m_samples.clear();
    }

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
    void RecordInterval(uint32_t intervalUs) {
        m_intervals.push_back(intervalUs);
    }

    void Reset() {
        m_intervals.clear();
    }

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
        double mean = sum / m_intervals.size();
        avgIntv = static_cast<uint32_t>(std::round(mean));

        double varSum = 0.0;
        for (auto v : m_intervals) {
            double diff = v - mean;
            varSum += diff * diff;
        }
        double variance = varSum / (m_intervals.size() - 1);
        stdDev = static_cast<uint32_t>(std::round(std::sqrt(variance)));
    }

private:
    std::vector<uint32_t> m_intervals;
};
