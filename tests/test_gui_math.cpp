#include <cassert>
#include <cstdio>
#include <cmath>
#include "osupoint/canvas_math.h"
#include "osupoint/ipc_protocol.h"
#include "osupoint/config_manager.h"

using namespace osupoint::gui;

static void TestCanvasViewportMath() {
    std::printf("[test-gui] Running Canvas Viewport math tests...\n");

    CanvasRect rect{ 0.0f, 0.0f, 800.0f, 500.0f };
    CanvasViewport vp = ComputeCanvasViewport(rect, 152.0f, 95.0f, 10.0f);

    assert(vp.scale > 0.0f);
    assert(vp.active_bounds.left >= rect.left);
    assert(vp.active_bounds.right <= rect.right);
    assert(vp.active_bounds.top >= rect.top);
    assert(vp.active_bounds.bottom <= rect.bottom);

    float active_w = vp.active_bounds.Width();
    float active_h = vp.active_bounds.Height();
    float active_ratio = active_w / active_h;
    float phys_ratio = 152.0f / 95.0f;
    assert(std::abs(active_ratio - phys_ratio) < 1e-3f);

    std::printf("[test-gui] Canvas Viewport math tests PASSED\n");
}

static void TestRotatedCornersMath() {
    std::printf("[test-gui] Running Rotated Corners math tests...\n");

    CanvasRect rect{ 0.0f, 0.0f, 600.0f, 400.0f };
    CanvasViewport vp = ComputeCanvasViewport(rect, 152.0f, 95.0f, 10.0f);

    CanvasPoint corners[4];
    CanvasPoint center;

    // 0 deg rotation
    ComputeRotatedCorners(100.0f, 50.0f, 76.0f, 47.5f, 0.0f, vp, corners, center);

    assert(corners[0].x < corners[1].x); // TL.x < TR.x
    assert(corners[0].y < corners[3].y); // TL.y < BL.y
    assert(corners[1].y == corners[0].y); // Top edge is horizontal

    // 90 deg rotation
    ComputeRotatedCorners(100.0f, 50.0f, 76.0f, 47.5f, 90.0f, vp, corners, center);
    // At 90 deg, width is vertical, height is horizontal
    float edge_01 = std::sqrt(corners[0].DistSq(corners[1]));
    assert(std::abs(edge_01 - 100.0f * vp.scale) < 1e-2f);

    std::printf("[test-gui] Rotated Corners math tests PASSED\n");
}

static void TestHitTesting() {
    std::printf("[test-gui] Running Canvas Hit-Testing tests...\n");

    CanvasRect rect{ 0.0f, 0.0f, 600.0f, 400.0f };
    CanvasViewport vp = ComputeCanvasViewport(rect, 152.0f, 95.0f, 10.0f);

    CanvasPoint corners[4];
    CanvasPoint center;
    ComputeRotatedCorners(80.0f, 60.0f, 76.0f, 47.5f, 0.0f, vp, corners, center);

    float handle_radius = 8.0f;

    // Hit corner 0 (TL)
    DragMode hit0 = HitTestCanvas(corners[0], corners, center, handle_radius, false);
    assert(hit0 == DragMode::ResizeCorner0);

    // Hit corner 1 (TR)
    DragMode hit1 = HitTestCanvas(corners[1], corners, center, handle_radius, false);
    assert(hit1 == DragMode::ResizeCorner1);

    // Hit corner 2 (BR)
    DragMode hit2 = HitTestCanvas(corners[2], corners, center, handle_radius, false);
    assert(hit2 == DragMode::ResizeCorner2);

    // Hit corner 3 (BL)
    DragMode hit3 = HitTestCanvas(corners[3], corners, center, handle_radius, false);
    assert(hit3 == DragMode::ResizeCorner3);

    // Hit center
    DragMode hit_c = HitTestCanvas(center, corners, center, handle_radius, false);
    assert(hit_c == DragMode::MoveCenter);

    // Hit outside
    CanvasPoint outside{ 0.0f, 0.0f };
    DragMode hit_out = HitTestCanvas(outside, corners, center, handle_radius, false);
    assert(hit_out == DragMode::None);

    // Lock drag disables all hits
    DragMode hit_locked = HitTestCanvas(corners[0], corners, center, handle_radius, true);
    assert(hit_locked == DragMode::None);

    std::printf("[test-gui] Canvas Hit-Testing tests PASSED\n");
}

static void TestCornerResizeMath() {
    std::printf("[test-gui] Running Corner Resize & Aspect Ratio tests...\n");

    CanvasRect rect{ 0.0f, 0.0f, 600.0f, 400.0f };
    CanvasViewport vp = ComputeCanvasViewport(rect, 152.0f, 95.0f, 10.0f);

    CanvasPoint center_mm{ 76.0f, 47.5f };
    CanvasPoint center_px = { vp.active_bounds.left + center_mm.x * vp.scale,
                              vp.active_bounds.top + center_mm.y * vp.scale };

    float out_w = 0.0f, out_h = 0.0f;

    // Drag mouse 20mm right, 10mm down from center (no lock)
    CanvasPoint mouse_px = { center_px.x + 20.0f * vp.scale, center_px.y + 10.0f * vp.scale };
    ComputeCornerResize(mouse_px, center_mm, 0.0f, vp, false, 1.0f, 152.0f, 95.0f, out_w, out_h);

    assert(std::abs(out_w - 40.0f) < 1e-2f);
    assert(std::abs(out_h - 20.0f) < 1e-2f);

    // Drag with lock_ratio 16:9 (1.777778)
    ComputeCornerResize(mouse_px, center_mm, 0.0f, vp, true, 16.0f / 9.0f, 152.0f, 95.0f, out_w, out_h);
    assert(std::abs(out_w / out_h - (16.0f / 9.0f)) < 1e-3f);

    std::printf("[test-gui] Corner Resize & Aspect Ratio tests PASSED\n");
}

int main() {
    std::printf("=========================================\n");
    std::printf("osuPoint Linux GUI Headless Test Suite\n");
    std::printf("=========================================\n");

    TestCanvasViewportMath();
    TestRotatedCornersMath();
    TestHitTesting();
    TestCornerResizeMath();

    std::printf("=========================================\n");
    std::printf("ALL GUI UNIT TESTS PASSED CLEANLY (4/4)\n");
    std::printf("=========================================\n");
    return 0;
}
