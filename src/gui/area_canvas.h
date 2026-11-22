#pragma once

#include "osupoint/canvas_math.h"
#include <imgui.h>

namespace osupoint::gui {

struct AreaCanvasState {
    float phys_w_mm = 152.0f;
    float phys_h_mm = 95.0f;
    float width_mm = 51.0f;
    float height_mm = 36.56f;
    float cx_mm = 70.0f;
    float cy_mm = 60.0f;
    float rotation_deg = -3.0f;
    bool lock_ratio = false;
    float ratio = 1.77777778f;
    bool lock_drag = false;
    bool clip_area = false;
    bool antialiasing = true;
    float ui_area_opacity = 0.35f;
    float ui_outline_opacity = 1.0f;

    // Interaction tracking
    DragMode active_drag = DragMode::None;
    CanvasPoint drag_start_mouse{};
    float drag_orig_cx = 0.0f;
    float drag_orig_cy = 0.0f;
    float drag_orig_w = 0.0f;
    float drag_orig_h = 0.0f;
};

class AreaCanvas {
public:
    static bool Render(const char* label,
                       AreaCanvasState& state,
                       const ImVec4& accent_color,
                       const ImVec2& size = ImVec2(0, 0));
};

} // namespace osupoint::gui
