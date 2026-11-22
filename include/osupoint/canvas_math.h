#pragma once

#include <cmath>
#include <algorithm>
#include <cstdint>

namespace osupoint::gui {

constexpr float PI_F = 3.14159265358979323846f;

struct CanvasPoint {
    float x = 0.0f;
    float y = 0.0f;

    constexpr CanvasPoint() noexcept = default;
    constexpr CanvasPoint(float px, float py) noexcept : x(px), y(py) {}

    constexpr CanvasPoint operator+(const CanvasPoint& o) const noexcept { return {x + o.x, y + o.y}; }
    constexpr CanvasPoint operator-(const CanvasPoint& o) const noexcept { return {x - o.x, y - o.y}; }
    constexpr CanvasPoint operator*(float s) const noexcept { return {x * s, y * s}; }
    constexpr CanvasPoint operator/(float s) const noexcept { return {x / s, y / s}; }

    float DistSq(const CanvasPoint& o) const noexcept {
        float dx = x - o.x;
        float dy = y - o.y;
        return dx * dx + dy * dy;
    }
};

struct CanvasRect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    constexpr float Width() const noexcept { return right - left; }
    constexpr float Height() const noexcept { return bottom - top; }
    constexpr CanvasPoint Center() const noexcept { return {(left + right) * 0.5f, (top + bottom) * 0.5f}; }
};

enum class DragMode {
    None = -1,
    MoveCenter = 4,
    ResizeCorner0 = 0, // Top-Left
    ResizeCorner1 = 1, // Top-Right
    ResizeCorner2 = 2, // Bottom-Right
    ResizeCorner3 = 3  // Bottom-Left
};

struct CanvasViewport {
    CanvasRect canvas_bounds;
    CanvasRect active_bounds;
    CanvasRect bezel_bounds;
    float scale = 1.0f;
    float bezel_margin = 12.0f;
};

inline CanvasViewport ComputeCanvasViewport(CanvasRect canvas_rect, float phys_w_mm, float phys_h_mm, float bezel_margin = 12.0f) noexcept {
    CanvasViewport vp;
    vp.canvas_bounds = canvas_rect;
    vp.bezel_margin = bezel_margin;

    float pw = (phys_w_mm > 0.0f) ? phys_w_mm : 152.0f;
    float ph = (phys_h_mm > 0.0f) ? phys_h_mm : 95.0f;

    float avail_w = std::max(1.0f, canvas_rect.Width() - 2.0f * bezel_margin);
    float avail_h = std::max(1.0f, canvas_rect.Height() - 2.0f * bezel_margin);

    float scale_x = avail_w / pw;
    float scale_y = avail_h / ph;
    vp.scale = std::min(scale_x, scale_y);

    float act_w = pw * vp.scale;
    float act_h = ph * vp.scale;

    float off_x = (avail_w - act_w) * 0.5f;
    float off_y = (avail_h - act_h) * 0.5f;

    vp.active_bounds.left = canvas_rect.left + bezel_margin + off_x;
    vp.active_bounds.top = canvas_rect.top + bezel_margin + off_y;
    vp.active_bounds.right = vp.active_bounds.left + act_w;
    vp.active_bounds.bottom = vp.active_bounds.top + act_h;

    vp.bezel_bounds.left = vp.active_bounds.left - bezel_margin;
    vp.bezel_bounds.top = vp.active_bounds.top - bezel_margin;
    vp.bezel_bounds.right = vp.active_bounds.right + bezel_margin;
    vp.bezel_bounds.bottom = vp.active_bounds.bottom + bezel_margin;

    return vp;
}

inline void ComputeRotatedCorners(float w_mm, float h_mm, float cx_mm, float cy_mm, float rot_deg,
                                  const CanvasViewport& vp,
                                  CanvasPoint out_corners[4], CanvasPoint& out_center) noexcept {
    const float alpha = std::fmod(rot_deg, 360.0f) * (PI_F / 180.0f);
    const float cos_a = std::cos(alpha);
    const float sin_a = std::sin(alpha);

    const float hw = w_mm * 0.5f;
    const float hh = h_mm * 0.5f;

    const CanvasPoint local[4] = {
        { -hw, -hh }, // 0: TL
        {  hw, -hh }, // 1: TR
        {  hw,  hh }, // 2: BR
        { -hw,  hh }  // 3: BL
    };

    for (int i = 0; i < 4; ++i) {
        float rx = local[i].x * cos_a - local[i].y * sin_a;
        float ry = local[i].x * sin_a + local[i].y * cos_a;
        out_corners[i].x = vp.active_bounds.left + (cx_mm + rx) * vp.scale;
        out_corners[i].y = vp.active_bounds.top + (cy_mm + ry) * vp.scale;
    }

    out_center.x = vp.active_bounds.left + cx_mm * vp.scale;
    out_center.y = vp.active_bounds.top + cy_mm * vp.scale;
}

inline DragMode HitTestCanvas(CanvasPoint mouse,
                              const CanvasPoint corners[4],
                              CanvasPoint center,
                              float handle_radius,
                              bool lock_drag = false) noexcept {
    if (lock_drag) return DragMode::None;

    const float r_sq = handle_radius * handle_radius;
    float best_dist = r_sq;
    int best_corner = -1;

    for (int i = 0; i < 4; ++i) {
        float d2 = mouse.DistSq(corners[i]);
        if (d2 <= best_dist) {
            best_dist = d2;
            best_corner = i;
        }
    }

    if (best_corner == 0) return DragMode::ResizeCorner0;
    if (best_corner == 1) return DragMode::ResizeCorner1;
    if (best_corner == 2) return DragMode::ResizeCorner2;
    if (best_corner == 3) return DragMode::ResizeCorner3;

    if (mouse.DistSq(center) <= r_sq) {
        return DragMode::MoveCenter;
    }

    // Ray casting point-in-polygon
    bool inside = false;
    for (int i = 0, j = 3; i < 4; j = i++) {
        if (((corners[i].y > mouse.y) != (corners[j].y > mouse.y)) &&
            (mouse.x < (corners[j].x - corners[i].x) * (mouse.y - corners[i].y) /
                (corners[j].y - corners[i].y) + corners[i].x)) {
            inside = !inside;
        }
    }

    return inside ? DragMode::MoveCenter : DragMode::None;
}

inline CanvasPoint ProjectDragDelta(CanvasPoint mouse_delta, float rot_deg) noexcept {
    const float alpha = std::fmod(rot_deg, 360.0f) * (PI_F / 180.0f);
    const float cos_a = std::cos(alpha);
    const float sin_a = std::sin(alpha);
    return {
        mouse_delta.x * cos_a + mouse_delta.y * sin_a,
        -mouse_delta.x * sin_a + mouse_delta.y * cos_a
    };
}

inline void ComputeCornerResize(CanvasPoint mouse_pos, CanvasPoint center_mm, float rot_deg,
                                const CanvasViewport& vp, bool lock_ratio, float aspect_ratio,
                                float phys_w, float phys_h,
                                float& out_w, float& out_h) noexcept {
    if (vp.scale <= 1e-6f) return;

    float mouse_mm_x = (mouse_pos.x - vp.active_bounds.left) / vp.scale;
    float mouse_mm_y = (mouse_pos.y - vp.active_bounds.top) / vp.scale;

    float dx = mouse_mm_x - center_mm.x;
    float dy = mouse_mm_y - center_mm.y;

    const float alpha = std::fmod(rot_deg, 360.0f) * (PI_F / 180.0f);
    const float cos_a = std::cos(alpha);
    const float sin_a = std::sin(alpha);

    float lx = dx * cos_a + dy * sin_a;
    float ly = -dx * sin_a + dy * cos_a;

    float nw = std::abs(lx) * 2.0f;
    float nh = std::abs(ly) * 2.0f;

    const float abs_cos = std::abs(cos_a);
    const float abs_sin = std::abs(sin_a);

    if (lock_ratio && aspect_ratio > 1e-4f) {
        float max_w_x = phys_w / (abs_cos + abs_sin / aspect_ratio);
        float max_w_y = phys_h / (abs_sin + abs_cos / aspect_ratio);
        float max_w = std::min(max_w_x, max_w_y);

        nw = std::clamp(nw, 2.0f, max_w);
        nh = nw / aspect_ratio;
    } else {
        nw = std::clamp(nw, 2.0f, phys_w);
        nh = std::clamp(nh, 2.0f, phys_h);
    }

    out_w = nw;
    out_h = nh;
}

} // namespace osupoint::gui

namespace osupoint {
    using gui::CanvasPoint;
    using gui::CanvasRect;
    using gui::DragMode;
    using gui::CanvasViewport;
    using gui::ComputeCanvasViewport;
    using gui::ComputeRotatedCorners;
    using gui::HitTestCanvas;
    using gui::ProjectDragDelta;
    using gui::ComputeCornerResize;
}
