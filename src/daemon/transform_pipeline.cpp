#include "osupoint/transform_pipeline.h"
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace osupoint {

double ParseAspectRatio(std::string_view str) noexcept {
    if (str.empty()) return 16.0 / 9.0;

    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return 16.0 / 9.0;
    size_t last = str.find_last_not_of(" \t\r\n");
    std::string_view s = str.substr(first, last - first + 1);

    size_t sep = s.find_first_of(":/xX,");
    if (sep != std::string_view::npos) {
        std::string_view sNum = s.substr(0, sep);
        std::string_view sDen = s.substr(sep + 1);

        size_t n1 = sNum.find_first_not_of(" \t\r\n");
        size_t n2 = sNum.find_last_not_of(" \t\r\n");
        size_t d1 = sDen.find_first_not_of(" \t\r\n");
        size_t d2 = sDen.find_last_not_of(" \t\r\n");

        if (n1 != std::string_view::npos && d1 != std::string_view::npos) {
            sNum = sNum.substr(n1, n2 - n1 + 1);
            sDen = sDen.substr(d1, d2 - d1 + 1);

            char bufN[64] = {0};
            char bufD[64] = {0};
            if (sNum.size() < sizeof(bufN) && sDen.size() < sizeof(bufD)) {
                std::memcpy(bufN, sNum.data(), sNum.size());
                std::memcpy(bufD, sDen.data(), sDen.size());
                char* endN = nullptr;
                char* endD = nullptr;
                double num = std::strtod(bufN, &endN);
                double den = std::strtod(bufD, &endD);
                if (endN != bufN && endD != bufD && den > 1e-9 && num > 1e-9) {
                    return num / den;
                }
            }
        }
    }

    char buf[64] = {0};
    if (s.size() < sizeof(buf)) {
        std::memcpy(buf, s.data(), s.size());
        char* end = nullptr;
        double val = std::strtod(buf, &end);
        if (end != buf && val > 1e-9) {
            return val;
        }
    }

    return 16.0 / 9.0;
}

std::string MatchMonitorAspectRatio(int width, int height, double& out_ratio) noexcept {
    if (width <= 0 || height <= 0) {
        out_ratio = 16.0 / 9.0;
        return "16:9";
    }

    int g = std::gcd(width, height);
    if (g == 0) g = 1;

    int num = width / g;
    int den = height / g;
    out_ratio = static_cast<double>(width) / static_cast<double>(height);

    return std::to_string(num) + ":" + std::to_string(den);
}

void ComputeRotatedCorners(double w_mm, double h_mm, double cx_mm, double cy_mm,
                           double rot_deg, double phys_w, double phys_h,
                           int32_t max_x, int32_t max_y,
                           RotatedCorners& out_corners) noexcept {
    const double canon_deg = std::remainder(rot_deg, 360.0);
    const double alpha = canon_deg * (PI / 180.0);
    const double cos_a = std::cos(alpha);
    const double sin_a = std::sin(alpha);

    const double hw = w_mm * 0.5;
    const double hh = h_mm * 0.5;

    const Point2D local[4] = {
        { -hw, -hh }, // Top-Left
        {  hw, -hh }, // Top-Right
        {  hw,  hh }, // Bottom-Right
        { -hw,  hh }  // Bottom-Left
    };

    double min_x = 1e9, max_x_mm = -1e9;
    double min_y = 1e9, max_y_mm = -1e9;

    const double scale_x = (phys_w > 0.0) ? (static_cast<double>(max_x) / phys_w) : 100.0;
    const double scale_y = (phys_h > 0.0) ? (static_cast<double>(max_y) / phys_h) : 100.0;

    for (int i = 0; i < 4; ++i) {
        double xm = cx_mm + (local[i].x * cos_a - local[i].y * sin_a);
        double ym = cy_mm + (local[i].x * sin_a + local[i].y * cos_a);

        out_corners.corners_mm[i] = { xm, ym };
        out_corners.corners_raw[i] = { xm * scale_x, ym * scale_y };

        min_x = std::min(min_x, xm);
        max_x_mm = std::max(max_x_mm, xm);
        min_y = std::min(min_y, ym);
        max_y_mm = std::max(max_y_mm, ym);
    }

    out_corners.aabb_min_mm = { min_x, min_y };
    out_corners.aabb_max_mm = { max_x_mm, max_y_mm };
}

void ClampAreaToBounds(double& w_mm, double& h_mm, double& cx_mm, double& cy_mm,
                       double rot_deg, double phys_w, double phys_h,
                       bool lock_ratio, double ratio) noexcept {
    if (phys_w <= 0.0 || phys_h <= 0.0) return;

    w_mm = std::max(2.0, w_mm);
    h_mm = std::max(2.0, h_mm);

    const double canon_deg = std::remainder(rot_deg, 360.0);
    const double alpha = canon_deg * (PI / 180.0);
    const double abs_cos = std::abs(std::cos(alpha));
    const double abs_sin = std::abs(std::sin(alpha));

    if (lock_ratio && ratio > 1e-4) {
        double max_w_x = phys_w / (abs_cos + abs_sin / ratio);
        double max_w_y = phys_h / (abs_sin + abs_cos / ratio);
        double max_w = std::min(max_w_x, max_w_y);

        w_mm = std::clamp(w_mm, 2.0, max_w);
        h_mm = w_mm / ratio;
    } else {
        w_mm = std::clamp(w_mm, 2.0, phys_w);
        h_mm = std::clamp(h_mm, 2.0, phys_h);
    }

    double span_x = w_mm * abs_cos + h_mm * abs_sin;
    double span_y = w_mm * abs_sin + h_mm * abs_cos;

    double half_span_x = span_x * 0.5;
    double half_span_y = span_y * 0.5;

    if (span_x <= phys_w) {
        cx_mm = std::clamp(cx_mm, half_span_x, phys_w - half_span_x);
    } else {
        cx_mm = phys_w * 0.5;
    }

    if (span_y <= phys_h) {
        cy_mm = std::clamp(cy_mm, half_span_y, phys_h - half_span_y);
    } else {
        cy_mm = phys_h * 0.5;
    }
}

} // namespace osupoint
