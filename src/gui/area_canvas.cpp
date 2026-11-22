#include "area_canvas.h"
#include <cstdio>
#include <algorithm>

namespace osupoint::gui {

bool AreaCanvas::Render(const char* label,
                        AreaCanvasState& state,
                        const ImVec4& accent_color,
                        const ImVec2& size) {
    bool value_changed = false;

    ImVec2 avail_size = size;
    if (avail_size.x <= 0.0f) avail_size.x = ImGui::GetContentRegionAvail().x;
    if (avail_size.y <= 0.0f) avail_size.y = ImGui::GetContentRegionAvail().y;

    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label, avail_size);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();

    CanvasRect canvas_rect{
        canvas_pos.x,
        canvas_pos.y,
        canvas_pos.x + avail_size.x,
        canvas_pos.y + avail_size.y
    };

    CanvasViewport vp = ComputeCanvasViewport(canvas_rect, state.phys_w_mm, state.phys_h_mm, 8.0f);

    CanvasPoint corners[4];
    CanvasPoint center;
    ComputeRotatedCorners(state.width_mm, state.height_mm, state.cx_mm, state.cy_mm, state.rotation_deg,
                          vp, corners, center);

    CanvasPoint mouse_pt{io.MousePos.x, io.MousePos.y};
    float handle_radius = 4.5f;

    // Check interaction
    if (!state.lock_drag) {
        if (ImGui::IsItemHovered()) {
            if (state.active_drag == DragMode::None && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                DragMode hit = HitTestCanvas(mouse_pt, corners, center, handle_radius, false);
                if (hit != DragMode::None) {
                    state.active_drag = hit;
                    state.drag_start_mouse = mouse_pt;
                    state.drag_orig_cx = state.cx_mm;
                    state.drag_orig_cy = state.cy_mm;
                    state.drag_orig_w = state.width_mm;
                    state.drag_orig_h = state.height_mm;
                }
            }
        }

        if (state.active_drag != DragMode::None) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (state.active_drag == DragMode::MoveCenter) {
                    float dx_px = mouse_pt.x - state.drag_start_mouse.x;
                    float dy_px = mouse_pt.y - state.drag_start_mouse.y;
                    float dx_mm = (vp.scale > 1e-6f) ? (dx_px / vp.scale) : 0.0f;
                    float dy_mm = (vp.scale > 1e-6f) ? (dy_px / vp.scale) : 0.0f;

                    float ncx = std::clamp(state.drag_orig_cx + dx_mm, 0.0f, state.phys_w_mm);
                    float ncy = std::clamp(state.drag_orig_cy + dy_mm, 0.0f, state.phys_h_mm);

                    if (ncx != state.cx_mm || ncy != state.cy_mm) {
                        state.cx_mm = ncx;
                        state.cy_mm = ncy;
                        value_changed = true;
                    }
                } else {
                    // Corner resize
                    float nw = state.width_mm;
                    float nh = state.height_mm;
                    CanvasPoint center_mm{state.cx_mm, state.cy_mm};
                    ComputeCornerResize(mouse_pt, center_mm, state.rotation_deg, vp,
                                        state.lock_ratio, state.ratio,
                                        state.phys_w_mm, state.phys_h_mm,
                                        nw, nh);

                    if (nw != state.width_mm || nh != state.height_mm) {
                        state.width_mm = nw;
                        state.height_mm = nh;
                        value_changed = true;
                    }
                }

                // Recompute geometry
                ComputeRotatedCorners(state.width_mm, state.height_mm, state.cx_mm, state.cy_mm, state.rotation_deg,
                                      vp, corners, center);
            } else {
                state.active_drag = DragMode::None;
            }
        }
    }

    // Set mouse cursor according to hover
    if (ImGui::IsItemHovered() && !state.lock_drag && state.active_drag == DragMode::None) {
        DragMode hit = HitTestCanvas(mouse_pt, corners, center, handle_radius, false);
        if (hit == DragMode::MoveCenter) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        } else if (hit == DragMode::ResizeCorner0 || hit == DragMode::ResizeCorner2) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
        } else if (hit == DragMode::ResizeCorner1 || hit == DragMode::ResizeCorner3) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW);
        }
    }

    // --- Rendering ---
    // 1. Tablet Bezel (upstream #121216, border #282830)
    ImU32 col_bezel_bg = ImGui::GetColorU32(ImVec4(18.0f/255.0f, 18.0f/255.0f, 22.0f/255.0f, 1.0f));
    ImU32 col_bezel_border = ImGui::GetColorU32(ImVec4(40.0f/255.0f, 40.0f/255.0f, 48.0f/255.0f, 1.0f));
    draw_list->AddRectFilled(
        ImVec2(vp.bezel_bounds.left, vp.bezel_bounds.top),
        ImVec2(vp.bezel_bounds.right, vp.bezel_bounds.bottom),
        col_bezel_bg, 8.0f);
    draw_list->AddRect(
        ImVec2(vp.bezel_bounds.left, vp.bezel_bounds.top),
        ImVec2(vp.bezel_bounds.right, vp.bezel_bounds.bottom),
        col_bezel_border, 8.0f, 0, 1.0f);

    // 2. Active Tablet Work Surface (upstream #0C0C10, border #1E1E24)
    ImU32 col_surface_bg = ImGui::GetColorU32(ImVec4(12.0f/255.0f, 12.0f/255.0f, 16.0f/255.0f, 1.0f));
    ImU32 col_surface_border = ImGui::GetColorU32(ImVec4(30.0f/255.0f, 30.0f/255.0f, 36.0f/255.0f, 1.0f));
    draw_list->AddRectFilled(
        ImVec2(vp.active_bounds.left, vp.active_bounds.top),
        ImVec2(vp.active_bounds.right, vp.active_bounds.bottom),
        col_surface_bg, 4.0f);
    draw_list->AddRect(
        ImVec2(vp.active_bounds.left, vp.active_bounds.top),
        ImVec2(vp.active_bounds.right, vp.active_bounds.bottom),
        col_surface_border, 4.0f, 0, 1.0f);

    // Grid lines across surface (upstream #1C1C24, 50mm steps)
    ImU32 col_grid = ImGui::GetColorU32(ImVec4(28.0f/255.0f, 28.0f/255.0f, 36.0f/255.0f, 1.0f));
    for (float gx = 50.0f; gx < state.phys_w_mm; gx += 50.0f) {
        float x = vp.active_bounds.left + gx * vp.scale;
        draw_list->AddLine(ImVec2(x, vp.active_bounds.top), ImVec2(x, vp.active_bounds.bottom), col_grid, 1.0f);
    }
    for (float gy = 50.0f; gy < state.phys_h_mm; gy += 50.0f) {
        float y = vp.active_bounds.top + gy * vp.scale;
        draw_list->AddLine(ImVec2(vp.active_bounds.left, y), ImVec2(vp.active_bounds.right, y), col_grid, 1.0f);
    }

    // 3. Rotated Active Area Polygon
    ImVec2 poly[4];
    for (int i = 0; i < 4; ++i) {
        if (state.antialiasing) {
            poly[i] = ImVec2(corners[i].x, corners[i].y);
        } else {
            poly[i] = ImVec2(std::round(corners[i].x), std::round(corners[i].y));
        }
    }

    // Default fill alpha 0.22 (55/255), outline alpha 1.0
    float area_alpha = (state.ui_area_opacity > 0.0f) ? state.ui_area_opacity : 0.22f;
    ImVec4 fill_vec = accent_color;
    fill_vec.w = area_alpha;
    ImU32 col_area_fill = ImGui::GetColorU32(fill_vec);

    float outline_alpha = (state.ui_outline_opacity > 0.0f) ? state.ui_outline_opacity : 1.0f;
    ImVec4 outline_vec = accent_color;
    outline_vec.w = outline_alpha;
    ImU32 col_area_outline = ImGui::GetColorU32(outline_vec);

    const ImDrawListFlags saved_flags = draw_list->Flags;
    if (state.antialiasing) {
        draw_list->Flags |= (ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill);
    } else {
        draw_list->Flags &= ~(ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill);
    }

    draw_list->AddConvexPolyFilled(poly, 4, col_area_fill);
    draw_list->AddPolyline(poly, 4, col_area_outline, ImDrawFlags_Closed, 1.8f);

    draw_list->Flags = saved_flags;

    // 4. Center Crosshair (length 6px)
    float cross_len = 6.0f;
    draw_list->AddLine(ImVec2(center.x - cross_len, center.y), ImVec2(center.x + cross_len, center.y), col_area_outline, 1.5f);
    draw_list->AddLine(ImVec2(center.x, center.y - cross_len), ImVec2(center.x, center.y + cross_len), col_area_outline, 1.5f);

    // 5. Corner Handles (Rendered if not lock_drag)
    if (!state.lock_drag) {
        ImU32 col_handle_fill = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImU32 col_handle_border = col_area_outline;

        for (int i = 0; i < 4; ++i) {
            float dist_sq = mouse_pt.DistSq(corners[i]);
            float rad = (dist_sq <= handle_radius * handle_radius) ? (handle_radius + 1.5f) : handle_radius;
            draw_list->AddCircleFilled(poly[i], rad, col_handle_fill, 16);
            draw_list->AddCircle(poly[i], rad, col_handle_border, 16, 1.2f);
        }
    }

    // 6. Text Labels on Canvas
    char dim_str[64];
    std::snprintf(dim_str, sizeof(dim_str), "%.1f x %.1f mm (%.1f°)", state.width_mm, state.height_mm, state.rotation_deg);
    ImVec2 text_size = ImGui::CalcTextSize(dim_str);
    ImVec2 text_pos(vp.active_bounds.left + 8.0f, vp.active_bounds.bottom - text_size.y - 6.0f);
    draw_list->AddText(text_pos, ImGui::GetColorU32(ImVec4(0.85f, 0.88f, 0.92f, 0.70f)), dim_str);

    char phys_str[64];
    std::snprintf(phys_str, sizeof(phys_str), "Tablet: %.0f x %.0f mm", state.phys_w_mm, state.phys_h_mm);
    draw_list->AddText(ImVec2(vp.active_bounds.left + 8.0f, vp.active_bounds.top + 6.0f),
                       ImGui::GetColorU32(ImVec4(0.55f, 0.58f, 0.65f, 0.60f)), phys_str);

    return value_changed;
}

} // namespace osupoint::gui
