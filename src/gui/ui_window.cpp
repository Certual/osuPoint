#include "ui_window.h"
#include "osupoint/transform_pipeline.h"
#include "osupoint/tablet_spec.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <thread>
#include <filesystem>
#include "stb_image.h"

namespace osupoint::gui {

static constexpr int BASE_WIDTH = 640;
constexpr int BASE_HEIGHT = 330;

constexpr int LEFT_CARD_X = 10;
constexpr int LEFT_CARD_Y = 10;
constexpr int LEFT_CARD_W = 296;
constexpr int LEFT_CARD_H = 310;

constexpr int RIGHT_CARD_X = 316;
constexpr int RIGHT_CARD_Y = 10;
constexpr int RIGHT_CARD_W = 314;
constexpr int RIGHT_CARD_H = 310;

constexpr int PREVIEW_X = 326;
constexpr int PREVIEW_Y = 20;
constexpr int PREVIEW_MAX_W = 294;
constexpr int PREVIEW_MAX_H = 290;
constexpr int BEZEL = 6;

static std::string FormatDouble(double v) {
    char buf[32];
    if (std::abs(v - std::round(v)) < 1e-4) {
        std::snprintf(buf, sizeof(buf), "%.0f", v);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f", v);
    }
    return std::string(buf);
}

MainWindow::MainWindow(const std::string& config_path, GLFWwindow* glfw_win)
    : m_config_path(config_path), m_glfw_window(glfw_win) {

    ConfigManager::LoadFromFile(m_config_path, m_config);

    // Always ensure internal DriverService is running inside GUI process
    if (!DriverService::Instance().IsRunning()) {
        m_owns_driver = DriverService::Instance().Start(
            m_config_path,
            m_config.mouse_mode,
            m_config.spin_percent,
            -1,
            -1 // No forced affinity; kernel RT scheduler places on best available core
        );
    } else {
        m_owns_driver = true;
    }
    m_shm_client.Connect();

    SyncFromConfig();

    // Initial theme
    std::string custom_hex = "";
    if (m_config.palettes.count(m_config.palette)) {
        custom_hex = m_config.palettes[m_config.palette].accent;
    }
    Theme::Apply(m_config.theme_base, m_config.palette, m_config.ui_opacity, custom_hex);

    if (m_glfw_window) {
        float alpha = (m_config.ui_style == 1) ? (m_config.ui_opacity * 0.01f) : 1.0f;
        glfwSetWindowOpacity(m_glfw_window, std::clamp(alpha, 0.2f, 1.0f));
    }

    PushLiveConfig();

    if (!m_config.bg_image.empty()) {
        ReloadBackgroundTexture();
    }
}

MainWindow::~MainWindow() {
    if (m_bg_texture != 0) {
        glDeleteTextures(1, &m_bg_texture);
        m_bg_texture = 0;
    }
    if (m_owns_driver) {
        DriverService::Instance().Stop();
    }
}

void MainWindow::SyncFromConfig() {
    m_width_mm = m_config.width;
    m_height_mm = m_config.height;
    m_cx_mm = m_config.center_x;
    m_cy_mm = m_config.center_y;
    m_rotation_deg = m_config.rotation;
    m_lock_ratio = m_config.lock_ratio;
    m_lock_drag = m_config.lock_drag;
    m_clip_area = m_config.clip_area;
    m_pen_click = m_config.pen_click;
    m_barrel_button = m_config.barrel_button;
    m_active_tab = m_config.active_tab;

    std::snprintf(m_edit_w, sizeof(m_edit_w), "%s", FormatDouble(m_width_mm).c_str());
    std::snprintf(m_edit_h, sizeof(m_edit_h), "%s", FormatDouble(m_height_mm).c_str());
    std::snprintf(m_edit_cx, sizeof(m_edit_cx), "%s", FormatDouble(m_cx_mm).c_str());
    std::snprintf(m_edit_cy, sizeof(m_edit_cy), "%s", FormatDouble(m_cy_mm).c_str());
    std::snprintf(m_edit_rot, sizeof(m_edit_rot), "%s", FormatDouble(m_rotation_deg).c_str());
    std::snprintf(m_edit_ratio, sizeof(m_edit_ratio), "%s", m_config.ratio.c_str());

    TabletSpec spec = DriverService::Instance().GetTabletSpec();
    if (spec.phys_w > 0.0) m_phys_w_mm = spec.phys_w;
    if (spec.phys_h > 0.0) m_phys_h_mm = spec.phys_h;
}

void MainWindow::SyncToConfig() {
    m_config.width = m_width_mm;
    m_config.height = m_height_mm;
    m_config.center_x = m_cx_mm;
    m_config.center_y = m_cy_mm;
    m_config.rotation = m_rotation_deg;
    m_config.lock_ratio = m_lock_ratio;
    m_config.lock_drag = m_lock_drag;
    m_config.clip_area = m_clip_area;
    m_config.pen_click = m_pen_click;
    m_config.barrel_button = m_barrel_button;
    m_config.active_tab = m_active_tab;
    m_config.ratio = m_edit_ratio;

    ConfigManager::SaveToFile(m_config_path, m_config);
    m_dirty_config = false;
    m_ui_dirty = true;
    m_last_config_save = std::chrono::steady_clock::now();
}

void MainWindow::PushLiveConfig() {
    m_config.width = m_width_mm;
    m_config.height = m_height_mm;
    m_config.center_x = m_cx_mm;
    m_config.center_y = m_cy_mm;
    m_config.rotation = m_rotation_deg;
    m_config.lock_ratio = m_lock_ratio;
    m_config.lock_drag = m_lock_drag;
    m_config.clip_area = m_clip_area;
    m_config.pen_click = m_pen_click;
    m_config.barrel_button = m_barrel_button;

    TransformConfig tc{};
    TabletSpec spec = DriverService::Instance().GetTabletSpec();
    if (spec.max_x <= 0) {
        spec.max_x = 15200;
        spec.max_y = 9500;
        spec.phys_w = 152.0;
        spec.phys_h = 95.0;
    }
    m_config.ToTransformConfig(spec, tc);
    tc.mouse_mode = m_config.mouse_mode ? 1 : 0;

    DriverService::Instance().UpdateConfig(tc);
    if (m_shm_client.IsConnected()) {
        m_shm_client.WriteConfig(tc);
    }
}

void MainWindow::AutoMatchRatio() {
    int monW = 1920;
    int monH = 1080;
    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    if (primary) {
        const GLFWvidmode* mode = glfwGetVideoMode(primary);
        if (mode && mode->width > 0 && mode->height > 0) {
            monW = mode->width;
            monH = mode->height;
        }
    }

    auto gcd = [](int a, int b) {
        while (b) { int t = b; b = a % b; a = t; }
        return a;
    };
    int g = gcd(monW, monH);
    int rw = monW / g;
    int rh = monH / g;

    if (rw <= 64 && rh <= 64) {
        std::snprintf(m_edit_ratio, sizeof(m_edit_ratio), "%d:%d", rw, rh);
    } else {
        std::snprintf(m_edit_ratio, sizeof(m_edit_ratio), "%.4f", static_cast<double>(monW) / monH);
    }

    double r = static_cast<double>(monW) / static_cast<double>(monH);
    m_lock_ratio = true;
    m_height_mm = std::clamp(m_width_mm / r, 1.0, m_phys_h_mm);
    std::snprintf(m_edit_h, sizeof(m_edit_h), "%s", FormatDouble(m_height_mm).c_str());

    SyncToConfig();
    PushLiveConfig();
}

void MainWindow::AutoMatchAreaRatio() {
    if (m_width_mm <= 0.001 || m_height_mm <= 0.001) return;

    double r = m_width_mm / m_height_mm;
    static const struct { int w, h; } kStandardRatios[] = {
        { 16, 9 }, { 16, 10 }, { 4, 3 }, { 21, 9 }, { 3, 2 }, { 5, 4 }, { 1, 1 }
    };
    bool matched = false;
    for (const auto& sr : kStandardRatios) {
        double stdR = static_cast<double>(sr.w) / sr.h;
        if (std::abs(r - stdR) < 0.015) {
            std::snprintf(m_edit_ratio, sizeof(m_edit_ratio), "%d:%d", sr.w, sr.h);
            matched = true;
            break;
        }
    }
    if (!matched) {
        std::snprintf(m_edit_ratio, sizeof(m_edit_ratio), "%.4f", r);
    }

    m_lock_ratio = true;
    SyncToConfig();
    PushLiveConfig();
}

void MainWindow::ResetToFullArea() {
    m_width_mm = m_phys_w_mm;
    m_height_mm = m_phys_h_mm;
    m_cx_mm = m_phys_w_mm * 0.5;
    m_cy_mm = m_phys_h_mm * 0.5;
    m_rotation_deg = 0.0;

    std::snprintf(m_edit_w, sizeof(m_edit_w), "%s", FormatDouble(m_width_mm).c_str());
    std::snprintf(m_edit_h, sizeof(m_edit_h), "%s", FormatDouble(m_height_mm).c_str());
    std::snprintf(m_edit_cx, sizeof(m_edit_cx), "%s", FormatDouble(m_cx_mm).c_str());
    std::snprintf(m_edit_cy, sizeof(m_edit_cy), "%s", FormatDouble(m_cy_mm).c_str());
    std::snprintf(m_edit_rot, sizeof(m_edit_rot), "0");

    SyncToConfig();
    PushLiveConfig();
}

void MainWindow::TogglePenClick() {
    m_pen_click = !m_pen_click;
    m_config.pen_click = m_pen_click;
    SyncToConfig();
    PushLiveConfig();
    m_ui_dirty = true;
}

void MainWindow::SetDebugOpen(bool open) {
    if (m_config.debug_mode != open) {
        m_config.debug_mode = open;
        PushLiveConfig();
        ConfigManager::SaveToFile(m_config_path, m_config);
        m_ui_dirty = true;
    }
}

std::string MainWindow::GetWindowTitle() const {
    if (!m_telemetry.connected) {
        return "osu!Point - [Waiting for tablet...]";
    }

    char title[256];
    const char* dev_name = m_telemetry.device_name[0] ? m_telemetry.device_name : "Tablet";

    if (m_config.debug_mode) {
        std::snprintf(title, sizeof(title), "%s | %.0f Hz | lat:%.0fus | jit:\xC2\xB1%.0fus",
                      dev_name, m_telemetry.current_hz, m_telemetry.latency_p50_us, m_telemetry.jitter_us);
    } else {
        std::snprintf(title, sizeof(title), "%s", dev_name);
    }
    return std::string(title);
}

void MainWindow::Render() {
    {
        std::lock_guard<std::mutex> lock(m_bg_mutex);
        if (!m_pending_bg_path.empty()) {
            std::string p = m_pending_bg_path;
            m_pending_bg_path.clear();
            SetBackgroundImage(p);
        }
    }

    // Read live telemetry
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_telemetry_read).count() >= 50) {
        m_last_telemetry_read = now;
        if (m_owns_driver) {
            DriverService::Instance().ReadTelemetry(m_telemetry);
        } else if (m_shm_client.IsConnected()) {
            m_shm_client.ReadTelemetry(m_telemetry);
        }
    }

    // Read live debug diagnostics if debug mode active
    if (m_config.debug_mode && std::chrono::duration_cast<std::chrono::milliseconds>(now - m_last_diagnostics_read).count() >= 30) {
        m_last_diagnostics_read = now;
        if (m_owns_driver) {
            DriverService::Instance().ReadDiagnostics(m_debug_snapshot);
        } else if (m_shm_client.IsConnected()) {
            m_shm_client.ReadDiagnostics(m_debug_snapshot);
        }
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus |
                                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("osuPointUpstreamWindow", nullptr, window_flags);

    // Compute UI scale from window dimensions (upstream base: 640x330)
    float sc = std::max(1.0f, std::min(viewport->Size.x / static_cast<float>(BASE_WIDTH),
                                       viewport->Size.y / static_cast<float>(BASE_HEIGHT)));

    // 1. Draw Left and Right Cards with exact upstream bounds
    RenderCards(sc);

    // 2. Draw Top Main Tabs [ Area ] [ Settings ] [ Misc ]
    RenderMainTabs(sc);

    // 3. Draw Tab Contents
    if (m_active_tab == 0) {
        RenderAreaControls(sc);
    } else if (m_active_tab == 1) {
        RenderSettingsSubtabs(sc);
        if (m_settings_subtab == 0) {
            RenderTuneControls(sc);
        } else if (m_settings_subtab == 1) {
            RenderLookControls(sc);
        } else {
            RenderThemeControls(sc);
        }
    } else {
        RenderMiscControls(sc);
    }

    // 4. Draw Right Card Tablet Preview
    RenderTabletPreview(sc);

    ImGui::End();
    ImGui::PopStyleVar();

    if (m_dirty_config) {
        SyncToConfig();
        PushLiveConfig();
    }
}

void MainWindow::RenderCards(float sc) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto S = [sc](int v) noexcept -> float { return std::round(v * sc); };

    // Fill Window Background (#0E0E11)
    ImVec2 win_pos = ImGui::GetWindowPos();
    ImVec2 win_sz = ImGui::GetWindowSize();
    dl->AddRectFilled(win_pos, ImVec2(win_pos.x + win_sz.x, win_pos.y + win_sz.y),
                      IM_COL32(14, 14, 17, 255));

    // Render Background Image if loaded
    if (m_bg_texture != 0 && m_bg_tex_w > 0 && m_bg_tex_h > 0) {
        float win_w = win_sz.x;
        float win_h = win_sz.y;
        float target_aspect = win_w / (win_h > 0.0f ? win_h : 1.0f);
        float img_aspect = static_cast<float>(m_bg_tex_w) / static_cast<float>(m_bg_tex_h);

        float uv0_x = 0.0f, uv0_y = 0.0f, uv1_x = 1.0f, uv1_y = 1.0f;
        if (img_aspect > target_aspect) {
            float visible_w = target_aspect / img_aspect;
            uv0_x = (1.0f - visible_w) * 0.5f;
            uv1_x = uv0_x + visible_w;
        } else if (img_aspect < target_aspect) {
            float visible_h = img_aspect / target_aspect;
            uv0_y = (1.0f - visible_h) * 0.5f;
            uv1_y = uv0_y + visible_h;
        }

        dl->AddImage(static_cast<ImTextureID>(m_bg_texture),
                     win_pos, ImVec2(win_pos.x + win_w, win_pos.y + win_h),
                     ImVec2(uv0_x, uv0_y), ImVec2(uv1_x, uv1_y),
                     IM_COL32(255, 255, 255, 255));
    }

    std::string custom_hex = "";
    if (m_config.palettes.count(m_config.palette)) custom_hex = m_config.palettes[m_config.palette].accent;
    ImVec4 accent_vec = Theme::GetAccentColor(m_config.palette, custom_hex);
    ImU32 col_accent = ImGui::GetColorU32(accent_vec);

    ImU32 col_card_bg = (m_bg_texture != 0) ? IM_COL32(22, 22, 26, 215) : IM_COL32(22, 22, 26, 255);       // #16161A
    ImU32 col_card_border = m_config.ui_outline ? col_accent : IM_COL32(40, 40, 48, 255);
    float card_rad = S(8);

    // Left Card: (10, 10, 296, 310)
    ImVec2 left_p0(win_pos.x + S(LEFT_CARD_X), win_pos.y + S(LEFT_CARD_Y));
    ImVec2 left_p1(left_p0.x + S(LEFT_CARD_W), left_p0.y + S(LEFT_CARD_H));
    dl->AddRectFilled(left_p0, left_p1, col_card_bg, card_rad);
    dl->AddRect(left_p0, left_p1, col_card_border, card_rad, 0, 1.0f);

    // Right Card: (316, 10, 314, 310)
    ImVec2 right_p0(win_pos.x + S(RIGHT_CARD_X), win_pos.y + S(RIGHT_CARD_Y));
    ImVec2 right_p1(right_p0.x + S(RIGHT_CARD_W), right_p0.y + S(RIGHT_CARD_H));
    dl->AddRectFilled(right_p0, right_p1, col_card_bg, card_rad);
    dl->AddRect(right_p0, right_p1, col_card_border, card_rad, 0, 1.0f);
}

void MainWindow::RenderMainTabs(float sc) {
    auto S = [sc](int v) noexcept -> float { return std::round(v * sc); };

    OsuPointConfig& cfg = m_config;
    std::string custom_hex = "";
    if (m_config.palettes.count(m_config.palette)) custom_hex = m_config.palettes[m_config.palette].accent;
    ImVec4 accent_vec = Theme::GetAccentColor(m_config.palette, custom_hex);
    ImU32 col_accent = ImGui::GetColorU32(accent_vec);

    ImVec2 win_pos = ImGui::GetWindowPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // 3 Main Tabs: [ Area ] [ Settings ] [ Misc ] (Exact Build 6 coordinates)
    struct TabDef {
        int x, y, w, h;
        const char* label;
        int idx;
    };
    TabDef tabs[3] = {
        { 18,  18, 90, 24, "Area", 0 },
        { 113, 18, 90, 24, "Settings", 1 },
        { 208, 18, 90, 24, "Misc", 2 }
    };

    for (const auto& tab : tabs) {
        ImVec2 p0(win_pos.x + S(tab.x), win_pos.y + S(tab.y));
        ImVec2 p1(p0.x + S(tab.w), p0.y + S(tab.h));

        bool is_sel = (m_active_tab == tab.idx);
        ImU32 bg_col = is_sel ? IM_COL32(36, 36, 44, 255) : IM_COL32(26, 26, 32, 255);
        ImU32 border_col = is_sel ? col_accent : IM_COL32(40, 40, 48, 255);
        ImU32 text_col = is_sel ? IM_COL32(255, 255, 255, 255) : IM_COL32(190, 190, 205, 255);

        ImGui::SetCursorScreenPos(p0);
        char btn_id[32];
        std::snprintf(btn_id, sizeof(btn_id), "##mainTabBtn%d", tab.idx);
        if (ImGui::InvisibleButton(btn_id, ImVec2(S(tab.w), S(tab.h)))) {
            m_active_tab = tab.idx;
            cfg.active_tab = m_active_tab;
            ConfigManager::SaveToFile(m_config_path, cfg);
        }

        bool hovered = ImGui::IsItemHovered();
        if (hovered && !is_sel) {
            bg_col = IM_COL32(42, 42, 52, 255);
            text_col = IM_COL32(245, 245, 250, 255);
        }

        dl->AddRectFilled(p0, p1, bg_col, S(6));
        dl->AddRect(p0, p1, border_col, S(6), 0, 1.0f);

        // Active tab underline bar (Build 6 exact: left+8 to right-8)
        if (is_sel) {
            dl->AddLine(ImVec2(p0.x + S(8), p1.y - S(2.0f)),
                        ImVec2(p1.x - S(8), p1.y - S(2.0f)),
                        col_accent, S(2.0f));
        }

        // Centered text
        ImVec2 txt_sz = ImGui::CalcTextSize(tab.label);
        ImVec2 txt_pos(p0.x + (S(tab.w) - txt_sz.x) * 0.5f,
                       p0.y + (S(tab.h) - txt_sz.y) * 0.5f);
        dl->AddText(txt_pos, text_col, tab.label);
    }
}

void MainWindow::RenderSettingsSubtabs(float sc) {
    auto S = [sc](int v) noexcept -> float { return std::round(v * sc); };

    std::string custom_hex = "";
    if (m_config.palettes.count(m_config.palette)) custom_hex = m_config.palettes[m_config.palette].accent;
    ImVec4 accent_vec = Theme::GetAccentColor(m_config.palette, custom_hex);
    ImU32 col_accent = ImGui::GetColorU32(accent_vec);

    ImVec2 win_pos = ImGui::GetWindowPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // 3 Subtabs: [ Tune ] [ Look ] [ Theme ] at y = 48 (Exact Build 6 coordinates)
    struct SubTabDef {
        int x, y, w, h;
        const char* label;
        int idx;
    };
    SubTabDef subtabs[3] = {
        { 18,  48, 90, 22, "Tune", 0 },
        { 113, 48, 90, 22, "Look", 1 },
        { 208, 48, 90, 22, "Theme", 2 }
    };

    for (const auto& tab : subtabs) {
        ImVec2 p0(win_pos.x + S(tab.x), win_pos.y + S(tab.y));
        ImVec2 p1(p0.x + S(tab.w), p0.y + S(tab.h));

        bool is_sel = (m_settings_subtab == tab.idx);
        ImU32 bg_col = is_sel ? IM_COL32(36, 36, 44, 255) : IM_COL32(26, 26, 32, 255);
        ImU32 border_col = is_sel ? col_accent : IM_COL32(40, 40, 48, 255);
        ImU32 text_col = is_sel ? IM_COL32(255, 255, 255, 255) : IM_COL32(190, 190, 205, 255);

        ImGui::SetCursorScreenPos(p0);
        char btn_id[32];
        std::snprintf(btn_id, sizeof(btn_id), "##subTabBtn%d", tab.idx);
        if (ImGui::InvisibleButton(btn_id, ImVec2(S(tab.w), S(tab.h)))) {
            m_settings_subtab = tab.idx;
        }

        bool hovered = ImGui::IsItemHovered();
        if (hovered && !is_sel) {
            bg_col = IM_COL32(42, 42, 52, 255);
            text_col = IM_COL32(245, 245, 250, 255);
        }

        dl->AddRectFilled(p0, p1, bg_col, S(4));
        dl->AddRect(p0, p1, border_col, S(4), 0, 1.0f);

        // Active subtab underline bar (Build 6 exact: left+6 to right-6)
        if (is_sel) {
            dl->AddLine(ImVec2(p0.x + S(6), p1.y - S(2.0f)),
                        ImVec2(p1.x - S(6), p1.y - S(2.0f)),
                        col_accent, S(2.0f));
        }

        // Centered text
        ImVec2 txt_sz = ImGui::CalcTextSize(tab.label);
        ImVec2 txt_pos(p0.x + (S(tab.w) - txt_sz.x) * 0.5f,
                       p0.y + (S(tab.h) - txt_sz.y) * 0.5f);
        dl->AddText(txt_pos, text_col, tab.label);
    }
}

bool MainWindow::DrawDraggableValueButton(const char* id, const char* label,
                                          int x, int y, int w, int h, float sc,
                                          double& val, double min_val, double max_val, double step,
                                          double toggle_val, const char* format_str,
                                          const char* zero_label,
                                          bool is_integer) {
    auto S = [sc](int v) noexcept -> float { return std::round(v * sc); };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetWindowPos();
    ImGuiIO& io = ImGui::GetIO();

    ImVec2 p0(win_pos.x + S(x), win_pos.y + S(y));
    ImVec2 p1(p0.x + S(w), p0.y + S(h));

    std::string custom_hex = "";
    if (m_config.palettes.count(m_config.palette)) custom_hex = m_config.palettes[m_config.palette].accent;
    ImVec4 accent_vec = Theme::GetAccentColor(m_config.palette, custom_hex);
    ImU32 col_accent = ImGui::GetColorU32(accent_vec);

    ImGui::SetCursorScreenPos(p0);
    char btn_id[64];
    std::snprintf(btn_id, sizeof(btn_id), "##dragBtn_%s", id);
    (void)ImGui::InvisibleButton(btn_id, ImVec2(S(w), S(h)));

    bool hovered = ImGui::IsItemHovered();
    bool active = ImGui::IsItemActive();
    bool right_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);

    bool changed = false;

    // 1. Horizontal Dragging (~6px step)
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
        float dx = io.MouseDelta.x;
        m_drag_accum_px += dx;
        if (std::abs(m_drag_accum_px) >= 6.0f) {
            int steps = static_cast<int>(m_drag_accum_px / 6.0f);
            val += steps * step;
            if (!m_config.unlock_limits) {
                val = std::clamp(val, min_val, max_val);
            } else {
                val = (std::max)(val, 0.0);
            }
            m_drag_accum_px -= steps * 6.0f;
            m_was_dragged = true;
            changed = true;
        }
    }

    // 2. Click Toggle (when released without dragging)
    if (ImGui::IsItemDeactivated()) {
        if (!m_was_dragged && hovered) {
            if (val <= (min_val + 1e-4)) {
                val = toggle_val;
            } else {
                val = min_val;
            }
            changed = true;
        }
        m_was_dragged = false;
        m_drag_accum_px = 0.0f;
    }

    // 3. Mouse Wheel increment/decrement
    if (hovered && io.MouseWheel != 0.0f) {
        val += (io.MouseWheel > 0 ? 1 : -1) * step;
        if (!m_config.unlock_limits) {
            val = std::clamp(val, min_val, max_val);
        } else {
            val = (std::max)(val, 0.0);
        }
        changed = true;
    }

    // 4. Right-Click Modal Input Popup
    char popup_id[64];
    std::snprintf(popup_id, sizeof(popup_id), "##modal_%s", id);
    if (right_clicked) {
        m_active_modal_id = popup_id;
        if (is_integer) {
            std::snprintf(m_modal_num_buf, sizeof(m_modal_num_buf), "%d", static_cast<int>(std::round(val)));
        } else {
            std::snprintf(m_modal_num_buf, sizeof(m_modal_num_buf), "%.2f", val);
        }
        ImGui::OpenPopup(popup_id);
    }

    if (ImGui::BeginPopup(popup_id)) {
        ImGui::Text("%s", label);
        ImGui::SetNextItemWidth(S(100));
        bool enter = ImGui::InputText("##numInput", m_modal_num_buf, sizeof(m_modal_num_buf),
                                      ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("OK") || enter) {
            char* end = nullptr;
            double n = std::strtod(m_modal_num_buf, &end);
            if (end != m_modal_num_buf) {
                val = m_config.unlock_limits ? (std::max)(n, 0.0) : std::clamp(n, min_val, max_val);
                changed = true;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Visual appearance matching Windows v0.6.1 Build 6 owner-draw
    ImU32 bg_col = active ? IM_COL32(48, 48, 60, 255) : (hovered ? IM_COL32(42, 42, 52, 255) : IM_COL32(30, 30, 36, 255));
    ImU32 border_col = active ? col_accent : (hovered ? IM_COL32(70, 70, 85, 255) : IM_COL32(52, 52, 62, 255));
    dl->AddRectFilled(p0, p1, bg_col, S(5));
    dl->AddRect(p0, p1, border_col, S(5), 0, 1.0f);

    // Formatted label
    char buf[64];
    if (zero_label && val <= (min_val + 1e-4)) {
        std::snprintf(buf, sizeof(buf), "%s", zero_label);
    } else if (format_str) {
        if (is_integer) {
            std::snprintf(buf, sizeof(buf), format_str, static_cast<int>(std::round(val)));
        } else {
            std::snprintf(buf, sizeof(buf), format_str, val);
        }
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f", val);
    }

    ImVec2 txt_sz = ImGui::CalcTextSize(buf);
    ImVec2 txt_pos(p0.x + (S(w) - txt_sz.x) * 0.5f, p0.y + (S(h) - txt_sz.y) * 0.5f);
    dl->AddText(txt_pos, IM_COL32(245, 245, 250, 255), buf);

    if (changed) {
        m_dirty_config = true;
        ConfigManager::SaveToFile(m_config_path, m_config);
        PushLiveConfig();
    }
    return changed;
}

void MainWindow::RenderAreaControls(float sc) {
    auto S = [sc](int v) noexcept -> float { return std::round(v * sc); };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetWindowPos();

    OsuPointConfig& cfg = m_config;
    std::string custom_hex = "";
    if (cfg.palettes.count(cfg.palette)) custom_hex = cfg.palettes[cfg.palette].accent;
    ImVec4 accent_vec = Theme::GetAccentColor(cfg.palette, custom_hex);
    ImU32 col_accent = ImGui::GetColorU32(accent_vec);

    auto DrawLabel = [&](const char* text, int x, int y, int w, int h) {
        (void)w;
        ImVec2 p0(win_pos.x + S(x), win_pos.y + S(y));
        ImVec2 txt_pos(p0.x, p0.y + (S(h) - ImGui::GetTextLineHeight()) * 0.5f);
        dl->AddText(txt_pos, IM_COL32(185, 185, 200, 255), text);
    };

    auto DrawButton = [&](const char* id, const char* label, int x, int y, int w, int h) -> bool {
        ImGui::SetCursorScreenPos(ImVec2(win_pos.x + S(x), win_pos.y + S(y)));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(30.0f/255.0f, 30.0f/255.0f, 36.0f/255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(42.0f/255.0f, 42.0f/255.0f, 52.0f/255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(48.0f/255.0f, 48.0f/255.0f, 60.0f/255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(52.0f/255.0f, 52.0f/255.0f, 62.0f/255.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(5));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

        char full_id[64];
        std::snprintf(full_id, sizeof(full_id), "%s##%s", label, id);
        bool clicked = ImGui::Button(full_id, ImVec2(S(w), S(h)));

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
        return clicked;
    };

    auto DrawCheckbox = [&](const char* id, const char* label, bool& val, int x, int y, int w, int h) -> bool {
        ImVec2 p0(win_pos.x + S(x), win_pos.y + S(y));
        ImGui::SetCursorScreenPos(p0);
        bool clicked = ImGui::InvisibleButton(id, ImVec2(S(w), S(h)));
        if (clicked) val = !val;

        bool hovered = ImGui::IsItemHovered();
        float box_sz = S(14);
        float box_y = p0.y + (S(h) - box_sz) * 0.5f;

        ImU32 box_bg = val ? col_accent : (hovered ? IM_COL32(42, 42, 52, 255) : IM_COL32(30, 30, 36, 255));
        ImU32 box_border = val ? col_accent : IM_COL32(52, 52, 62, 255);

        dl->AddRectFilled(ImVec2(p0.x, box_y), ImVec2(p0.x + box_sz, box_y + box_sz), box_bg, S(4));
        dl->AddRect(ImVec2(p0.x, box_y), ImVec2(p0.x + box_sz, box_y + box_sz), box_border, S(4), 0, 1.0f);

        if (val) {
            ImVec2 chk_pts[3] = {
                ImVec2(p0.x + box_sz * 0.22f, box_y + box_sz * 0.50f),
                ImVec2(p0.x + box_sz * 0.44f, box_y + box_sz * 0.72f),
                ImVec2(p0.x + box_sz * 0.78f, box_y + box_sz * 0.28f)
            };
            dl->AddPolyline(chk_pts, 3, IM_COL32(255, 255, 255, 255), 0, S(2));
        }

        ImVec2 txt_pos(p0.x + box_sz + S(6), p0.y + (S(h) - ImGui::GetTextLineHeight()) * 0.5f);
        dl->AddText(txt_pos, IM_COL32(245, 245, 250, 255), label);
        return clicked;
    };

    auto DrawEdit = [&](const char* id, char* buf, size_t sz, int x, int y, int w, int h) -> bool {
        (void)h;
        ImGui::SetCursorScreenPos(ImVec2(win_pos.x + S(x), win_pos.y + S(y)));
        ImGui::SetNextItemWidth(S(w));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(30.0f/255.0f, 30.0f/255.0f, 36.0f/255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(52.0f/255.0f, 52.0f/255.0f, 62.0f/255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(245.0f/255.0f, 245.0f/255.0f, 250.0f/255.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(4));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(6), S(3.5f)));

        bool changed = ImGui::InputText(id, buf, sz, ImGuiInputTextFlags_CharsDecimal);

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(3);
        return changed;
    };

    // --- Tab 0 Area Controls (Exact Windows v0.6.1 Build 6 Layout) ---

    // 1. Display: (18, 49, 56, 20) + Combo (78, 47, 220, 24)
    DrawLabel("Display:", 18, 49, 56, 20);
    ImGui::SetCursorScreenPos(ImVec2(win_pos.x + S(78), win_pos.y + S(47)));
    ImGui::SetNextItemWidth(S(220));
    const char* mons[] = { "#1 (1920x1080) *", "Entire Desktop" };
    int cur_mon = (cfg.monitor < 0) ? 1 : 0;
    if (ImGui::Combo("##cboMon", &cur_mon, mons, 2)) {
        cfg.monitor = (cur_mon == 1) ? -1 : 0;
        ConfigManager::SaveToFile(m_config_path, cfg);
        PushLiveConfig();
    }

    // 2. Preset: (18, 75, 56, 20) + Combo (78, 73, 126, 24) + Save (208, 73, 43, 24) + Del (255, 73, 43, 24)
    DrawLabel("Preset:", 18, 75, 56, 20);
    ImGui::SetCursorScreenPos(ImVec2(win_pos.x + S(78), win_pos.y + S(73)));
    ImGui::SetNextItemWidth(S(126));

    std::vector<std::string> preset_names;
    for (const auto& [name, p] : cfg.presets) preset_names.push_back(name);
    if (preset_names.empty()) preset_names.push_back("Default");

    int sel_idx = 0;
    for (size_t i = 0; i < preset_names.size(); ++i) {
        if (preset_names[i] == cfg.active_preset) { sel_idx = static_cast<int>(i); break; }
    }

    if (ImGui::BeginCombo("##cboPreset", preset_names[sel_idx].c_str())) {
        for (size_t i = 0; i < preset_names.size(); ++i) {
            bool is_sel = (sel_idx == static_cast<int>(i));
            if (ImGui::Selectable(preset_names[i].c_str(), is_sel)) {
                cfg.active_preset = preset_names[i];
                if (cfg.presets.count(preset_names[i])) {
                    const auto& pr = cfg.presets[preset_names[i]];
                    m_width_mm = pr.width;
                    m_height_mm = pr.height;
                    m_cx_mm = pr.center_x;
                    m_cy_mm = pr.center_y;
                    m_rotation_deg = pr.rotation;
                    std::snprintf(m_edit_w, sizeof(m_edit_w), "%s", FormatDouble(m_width_mm).c_str());
                    std::snprintf(m_edit_h, sizeof(m_edit_h), "%s", FormatDouble(m_height_mm).c_str());
                    std::snprintf(m_edit_cx, sizeof(m_edit_cx), "%s", FormatDouble(m_cx_mm).c_str());
                    std::snprintf(m_edit_cy, sizeof(m_edit_cy), "%s", FormatDouble(m_cy_mm).c_str());
                    std::snprintf(m_edit_rot, sizeof(m_edit_rot), "%s", FormatDouble(m_rotation_deg).c_str());
                }
                ConfigManager::SaveToFile(m_config_path, cfg);
                PushLiveConfig();
            }
        }
        ImGui::EndCombo();
    }

    if (DrawButton("btnSavePreset", "Save", 208, 73, 43, 24)) {
        ImGui::OpenPopup("Save Preset##modal");
    }
    if (DrawButton("btnDelPreset", "Del", 255, 73, 43, 24)) {
        if (cfg.active_preset != "Default" && cfg.presets.size() > 1) {
            cfg.presets.erase(cfg.active_preset);
            cfg.active_preset = "Default";
            ConfigManager::SaveToFile(m_config_path, cfg);
        }
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(22.0f/255.0f, 22.0f/255.0f, 26.0f/255.0f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, accent_vec);

    if (ImGui::BeginPopupModal("Save Preset##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::TextColored(ImVec4(245.0f/255.0f, 245.0f/255.0f, 250.0f/255.0f, 1.0f), "Enter Preset Name:");
        ImGui::Spacing();
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(210.0f);
        bool enter_pressed = ImGui::InputText("##presetNameInput", m_new_preset_name, sizeof(m_new_preset_name), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Button, accent_vec);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent_vec.x * 1.15f, accent_vec.y * 1.15f, accent_vec.z * 1.15f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(accent_vec.x * 0.85f, accent_vec.y * 0.85f, accent_vec.z * 0.85f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.05f, 0.07f, 1.0f));
        bool do_save = ImGui::Button("Save", ImVec2(96, 26)) || enter_pressed;
        ImGui::PopStyleColor(4);

        if (do_save) {
            std::string name(m_new_preset_name);
            if (!name.empty()) {
                PresetConfig p{};
                p.name = name;
                p.width = m_width_mm;
                p.height = m_height_mm;
                p.center_x = m_cx_mm;
                p.center_y = m_cy_mm;
                p.rotation = m_rotation_deg;
                p.ratio = m_edit_ratio;
                p.monitor = cfg.monitor;
                p.palette = cfg.palette;
                cfg.presets[name] = p;
                cfg.active_preset = name;
                ConfigManager::SaveToFile(m_config_path, cfg);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(96, 26)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    // 3. W (18, 105, 24, 20) + Edit (44, 103, 110, 24)
    //    H (162, 105, 24, 20) + Edit (188, 103, 110, 24)
    DrawLabel("W:", 18, 105, 24, 20);
    if (DrawEdit("##editW", m_edit_w, sizeof(m_edit_w), 44, 103, 110, 24)) {
        double nw = ConfigManager::SafeStod(m_edit_w, m_width_mm);
        nw = cfg.unlock_limits ? (std::max)(nw, 0.1) : std::clamp(nw, 1.0, m_phys_w_mm);
        m_width_mm = nw;
        if (m_lock_ratio) {
            double r = ParseAspectRatio(m_edit_ratio);
            if (r > 1e-4) {
                m_height_mm = cfg.unlock_limits ? (nw / r) : std::clamp(nw / r, 1.0, m_phys_h_mm);
                std::snprintf(m_edit_h, sizeof(m_edit_h), "%s", FormatDouble(m_height_mm).c_str());
            }
        }
        m_dirty_config = true;
    }

    DrawLabel("H:", 162, 105, 24, 20);
    if (DrawEdit("##editH", m_edit_h, sizeof(m_edit_h), 188, 103, 110, 24)) {
        double nh = ConfigManager::SafeStod(m_edit_h, m_height_mm);
        nh = cfg.unlock_limits ? (std::max)(nh, 0.1) : std::clamp(nh, 1.0, m_phys_h_mm);
        m_height_mm = nh;
        if (m_lock_ratio) {
            double r = ParseAspectRatio(m_edit_ratio);
            if (r > 1e-4) {
                m_width_mm = cfg.unlock_limits ? (nh * r) : std::clamp(nh * r, 1.0, m_phys_w_mm);
                std::snprintf(m_edit_w, sizeof(m_edit_w), "%s", FormatDouble(m_width_mm).c_str());
            }
        }
        m_dirty_config = true;
    }

    // 4. X (18, 133, 24, 20) + Edit (44, 131, 110, 24)
    //    Y (162, 133, 24, 20) + Edit (188, 131, 110, 24)
    DrawLabel("X:", 18, 133, 24, 20);
    if (DrawEdit("##editCX", m_edit_cx, sizeof(m_edit_cx), 44, 131, 110, 24)) {
        double ncx = ConfigManager::SafeStod(m_edit_cx, m_cx_mm);
        m_cx_mm = cfg.unlock_limits ? ncx : std::clamp(ncx, 0.0, m_phys_w_mm);
        m_dirty_config = true;
    }

    DrawLabel("Y:", 162, 133, 24, 20);
    if (DrawEdit("##editCY", m_edit_cy, sizeof(m_edit_cy), 188, 131, 110, 24)) {
        double ncy = ConfigManager::SafeStod(m_edit_cy, m_cy_mm);
        m_cy_mm = cfg.unlock_limits ? ncy : std::clamp(ncy, 0.0, m_phys_h_mm);
        m_dirty_config = true;
    }

    // 5. Rot (18, 161, 24, 20) + Edit (44, 159, 110, 24)
    DrawLabel("Rot:", 18, 161, 24, 20);
    if (DrawEdit("##editRot", m_edit_rot, sizeof(m_edit_rot), 44, 159, 110, 24)) {
        double nrot = ConfigManager::SafeStod(m_edit_rot, m_rotation_deg);
        m_rotation_deg = cfg.unlock_limits ? nrot : std::clamp(nrot, 0.0, 360.0);
        m_dirty_config = true;
    }

    // 6. Ratio row (y=187): [ ] Ratio (18, 187, 56, 24)
    // When locked: Edit (78, 187, 66, 24) + Screen (148, 187, 73, 24) + Area (225, 187, 73, 24)
    // When unlocked: Screen (78, 187, 108, 24) + Area (190, 187, 108, 24) filling all remaining space
    if (DrawCheckbox("##chkRatio", "Ratio", m_lock_ratio, 18, 187, 56, 24)) {
        cfg.lock_ratio = m_lock_ratio;
        if (m_lock_ratio) {
            double r = ParseAspectRatio(m_edit_ratio);
            if (r > 1e-4) {
                m_height_mm = cfg.unlock_limits ? (m_width_mm / r) : std::clamp(m_width_mm / r, 1.0, m_phys_h_mm);
                std::snprintf(m_edit_h, sizeof(m_edit_h), "%s", FormatDouble(m_height_mm).c_str());
            }
        }
        ConfigManager::SaveToFile(m_config_path, cfg);
    }
    if (m_lock_ratio) {
        if (DrawEdit("##editRatio", m_edit_ratio, sizeof(m_edit_ratio), 78, 187, 66, 24)) {
            cfg.ratio = m_edit_ratio;
            double r = ParseAspectRatio(m_edit_ratio);
            if (r > 1e-4) {
                m_height_mm = cfg.unlock_limits ? (m_width_mm / r) : std::clamp(m_width_mm / r, 1.0, m_phys_h_mm);
                std::snprintf(m_edit_h, sizeof(m_edit_h), "%s", FormatDouble(m_height_mm).c_str());
            }
            m_dirty_config = true;
        }
        if (DrawButton("btnAutoRatio", "Screen", 148, 187, 73, 24)) {
            m_lock_ratio = true;
            cfg.lock_ratio = true;
            AutoMatchRatio();
        }
        if (DrawButton("btnAreaRatio", "Area", 225, 187, 73, 24)) {
            m_lock_ratio = true;
            cfg.lock_ratio = true;
            AutoMatchAreaRatio();
        }
    } else {
        if (DrawButton("btnAutoRatio", "Screen", 78, 187, 108, 24)) {
            m_lock_ratio = true;
            cfg.lock_ratio = true;
            AutoMatchRatio();
        }
        if (DrawButton("btnAreaRatio", "Area", 190, 187, 108, 24)) {
            m_lock_ratio = true;
            cfg.lock_ratio = true;
            AutoMatchAreaRatio();
        }
    }

    // 7. Bottom row (y=218): Lock (18, 218, 56, 22) + Clip (78, 218, 56, 22) + Click (138, 218, 56, 22) + Reset (202, 217, 96, 24)
    if (DrawCheckbox("##chkLockDrag", "Lock", m_lock_drag, 18, 218, 56, 22)) {
        cfg.lock_drag = m_lock_drag;
        ConfigManager::SaveToFile(m_config_path, cfg);
    }
    if (DrawCheckbox("##chkClipArea", "Clip", m_clip_area, 78, 218, 56, 22)) {
        cfg.clip_area = m_clip_area;
        ConfigManager::SaveToFile(m_config_path, cfg);
        PushLiveConfig();
    }
    if (DrawCheckbox("##chkPenClick", "Click", m_pen_click, 138, 218, 56, 22)) {
        cfg.pen_click = m_pen_click;
        ConfigManager::SaveToFile(m_config_path, cfg);
        PushLiveConfig();
    }
    if (DrawButton("btnFullReset", "Reset", 202, 217, 96, 24)) {
        ResetToFullArea();
    }
}

void MainWindow::RenderTuneControls(float sc) {
    auto S = [sc](int v) noexcept -> float { return std::round(v * sc); };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetWindowPos();

    OsuPointConfig& cfg = m_config;

    auto DrawLabel = [&](const char* text, int x, int y, int w, int h) {
        (void)w;
        ImVec2 p0(win_pos.x + S(x), win_pos.y + S(y));
        ImVec2 txt_pos(p0.x, p0.y + (S(h) - ImGui::GetTextLineHeight()) * 0.5f);
        dl->AddText(txt_pos, IM_COL32(185, 185, 200, 255), text);
    };

    // Tune Subtab Controls (Exact Windows v0.6.1 Build 6 Coordinates & Single Wide Draggable Buttons)
    // Row 1 (y=80..82): Extrapolate: (18, 82, 88, 20) + btnExtraVal (112, 80, 124, 22)
    DrawLabel("Extrapolate:", 18, 82, 88, 20);
    double max_tau = cfg.unlock_limits ? 100.0 : 10.0;
    if (DrawDraggableValueButton("btnExtraVal", "Extrapolate (mm)", 112, 80, 124, 22, sc,
                                cfg.extrapolation_mm, 0.0, max_tau, 0.1, 2.5, "%.1f mm", "Off")) {
        PushLiveConfig();
        ConfigManager::SaveToFile(m_config_path, cfg);
    }

    // Row 2 (y=108..110): Anti-Noise: (18, 110, 88, 20) + btnAntiNoiseVal (112, 108, 124, 22)
    DrawLabel("Anti-Noise:", 18, 110, 88, 20);
    int max_filter = cfg.unlock_limits ? 200 : 100;
    if (DrawDraggableValueButton("btnAntiNoiseVal", "Anti-Noise (%)", 112, 108, 124, 22, sc,
                                cfg.filter, 0, max_filter, 5, 25, "%d%%", "Off")) {
        PushLiveConfig();
        ConfigManager::SaveToFile(m_config_path, cfg);
    }

    // Row 3 (y=136..138): Motion Filter: (18, 138, 88, 20) + btnMotionFilterVal (112, 136, 124, 22)
    DrawLabel("Motion Filter:", 18, 138, 88, 20);
    int max_mfilter = cfg.unlock_limits ? 200 : 100;
    if (DrawDraggableValueButton("btnMotionFilterVal", "Motion Filter (%)", 112, 136, 124, 22, sc,
                                cfg.motion_filter, 0, max_mfilter, 5, 40, "%d%%", "Off")) {
        PushLiveConfig();
        ConfigManager::SaveToFile(m_config_path, cfg);
    }

    // Row 4 (y=164..166): Low Latency: (18, 166, 88, 20) + btnSpinVal (112, 164, 124, 22)
    DrawLabel("Low Latency:", 18, 166, 88, 20);
    int max_spin = cfg.unlock_limits ? 200 : 100;
    if (DrawDraggableValueButton("btnSpinVal", "Low Latency (%)", 112, 164, 124, 22, sc,
                                cfg.spin_percent, 0, max_spin, 25, 100, "%d%%", "Eco (0%)")) {
        PushLiveConfig();
        ConfigManager::SaveToFile(m_config_path, cfg);
    }

    // Row 5 (y=192..194): Ratio Digits: (18, 194, 88, 20) + btnRatioPrecVal (112, 192, 124, 22)
    DrawLabel("Ratio Digits:", 18, 194, 88, 20);
    int max_prec = cfg.unlock_limits ? 16 : 8;
    if (DrawDraggableValueButton("btnRatioPrecVal", "Ratio Digits", 112, 192, 124, 22, sc,
                                cfg.ratio_precision, 1, max_prec, 1, 8, "%d digits")) {
        ConfigManager::SaveToFile(m_config_path, cfg);
    }

    // Row 6 (y=220..222): Input Mode: (18, 222, 88, 20) + btnInputModeToggle (112, 220, 124, 22)
    DrawLabel("Input Mode:", 18, 222, 88, 20);
    const char* mode_label = cfg.mouse_mode ? "Mouse" : "Stylus";
    ImGui::SetCursorScreenPos(ImVec2(win_pos.x + S(112), win_pos.y + S(220)));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(30.0f/255.0f, 30.0f/255.0f, 36.0f/255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(42.0f/255.0f, 42.0f/255.0f, 52.0f/255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(48.0f/255.0f, 48.0f/255.0f, 60.0f/255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(52.0f/255.0f, 52.0f/255.0f, 62.0f/255.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(5));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    char full_mode_id[64];
    std::snprintf(full_mode_id, sizeof(full_mode_id), "%s###btnInputModeToggle", mode_label);
    if (ImGui::Button(full_mode_id, ImVec2(S(124), S(22)))) {
        cfg.mouse_mode = !cfg.mouse_mode;
        PushLiveConfig();
        ConfigManager::SaveToFile(m_config_path, cfg);
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
}

void MainWindow::RenderLookControls(float sc) {
    auto S = [sc](int v) noexcept -> float { return std::round(v * sc); };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetWindowPos();

    OsuPointConfig& cfg = m_config;
    std::string custom_hex = "";
    if (cfg.palettes.count(cfg.palette)) custom_hex = cfg.palettes[cfg.palette].accent;
    ImVec4 accent_vec = Theme::GetAccentColor(cfg.palette, custom_hex);
    ImU32 col_accent = ImGui::GetColorU32(accent_vec);

    auto DrawLabel = [&](const char* text, int x, int y, int w, int h) {
        (void)w;
        ImVec2 p0(win_pos.x + S(x), win_pos.y + S(y));
        ImVec2 txt_pos(p0.x, p0.y + (S(h) - ImGui::GetTextLineHeight()) * 0.5f);
        dl->AddText(txt_pos, IM_COL32(185, 185, 200, 255), text);
    };

    auto DrawButton = [&](const char* id, const char* label, int x, int y, int w, int h) -> bool {
        ImGui::SetCursorScreenPos(ImVec2(win_pos.x + S(x), win_pos.y + S(y)));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(30.0f/255.0f, 30.0f/255.0f, 36.0f/255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(42.0f/255.0f, 42.0f/255.0f, 52.0f/255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(48.0f/255.0f, 48.0f/255.0f, 60.0f/255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(52.0f/255.0f, 52.0f/255.0f, 62.0f/255.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(5));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

        char full_id[64];
        std::snprintf(full_id, sizeof(full_id), "%s##%s", label, id);
        bool clicked = ImGui::Button(full_id, ImVec2(S(w), S(h)));

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
        return clicked;
    };

    auto DrawCheckbox = [&](const char* id, const char* label, bool& val, int x, int y, int w, int h) -> bool {
        ImVec2 p0(win_pos.x + S(x), win_pos.y + S(y));
        ImGui::SetCursorScreenPos(p0);
        bool clicked = ImGui::InvisibleButton(id, ImVec2(S(w), S(h)));
        if (clicked) val = !val;

        bool hovered = ImGui::IsItemHovered();
        float box_sz = S(14);
        float box_y = p0.y + (S(h) - box_sz) * 0.5f;

        ImU32 box_bg = val ? col_accent : (hovered ? IM_COL32(42, 42, 52, 255) : IM_COL32(30, 30, 36, 255));
        ImU32 box_border = val ? col_accent : IM_COL32(52, 52, 62, 255);

        dl->AddRectFilled(ImVec2(p0.x, box_y), ImVec2(p0.x + box_sz, box_y + box_sz), box_bg, S(4));
        dl->AddRect(ImVec2(p0.x, box_y), ImVec2(p0.x + box_sz, box_y + box_sz), box_border, S(4), 0, 1.0f);

        if (val) {
            ImVec2 chk_pts[3] = {
                ImVec2(p0.x + box_sz * 0.22f, box_y + box_sz * 0.50f),
                ImVec2(p0.x + box_sz * 0.44f, box_y + box_sz * 0.72f),
                ImVec2(p0.x + box_sz * 0.78f, box_y + box_sz * 0.28f)
            };
            dl->AddPolyline(chk_pts, 3, IM_COL32(255, 255, 255, 255), 0, S(2));
        }

        ImVec2 txt_pos(p0.x + box_sz + S(6), p0.y + (S(h) - ImGui::GetTextLineHeight()) * 0.5f);
        dl->AddText(txt_pos, IM_COL32(245, 245, 250, 255), label);
        return clicked;
    };

    // Look Subtab Controls (Exact Windows v0.6.1 Build 6 Coordinates)
    // Row 1 (y=78..80): Style: (18, 80, 44, 20) + Combo (66, 78, 88, 22) + Blur: (162, 80, 58, 20) + Combo (224, 78, 74, 22)
    DrawLabel("Style:", 18, 80, 44, 20);
    ImGui::SetCursorScreenPos(ImVec2(win_pos.x + S(66), win_pos.y + S(78)));
    ImGui::SetNextItemWidth(S(88));
    const char* styles[] = { "Normal", "Acrylic" };
    int cur_style = std::clamp(cfg.ui_style, 0, 1);
    if (ImGui::Combo("##cboStyle", &cur_style, styles, 2)) {
        cfg.ui_style = cur_style;
        if (m_glfw_window) {
            float alpha = (cfg.ui_style == 1) ? (cfg.ui_opacity * 0.01f) : 1.0f;
            glfwSetWindowOpacity(m_glfw_window, std::clamp(alpha, 0.2f, 1.0f));
        }
        ConfigManager::SaveToFile(m_config_path, cfg);
    }

    DrawLabel("Blur:", 162, 80, 58, 20);
    ImGui::SetCursorScreenPos(ImVec2(win_pos.x + S(224), win_pos.y + S(78)));
    ImGui::SetNextItemWidth(S(74));
    const char* blur_types[] = { "Gaussian", "Acrylic", "Mosaic", "Radial" };
    int cur_blur_type = std::clamp(cfg.bg_blur_type, 0, 3);
    if (ImGui::Combo("##cboBlurType", &cur_blur_type, blur_types, 4)) {
        cfg.bg_blur_type = cur_blur_type;
        ConfigManager::SaveToFile(m_config_path, cfg);
    }

    // Row 2 (y=106..108): Opacity: (18, 108, 44, 20) + Draggable (66, 106, 88, 22) + Radius: (162, 108, 58, 20) + Draggable (224, 106, 74, 22)
    DrawLabel("Opacity:", 18, 108, 44, 20);
    if (DrawDraggableValueButton("btnOpacityVal", "Opacity (%)", 66, 106, 88, 22, sc,
                                cfg.ui_opacity, 10, 100, 5, 69, "%d%%")) {
        Theme::Apply(cfg.theme_base, cfg.palette, cfg.ui_opacity, custom_hex);
        if (m_glfw_window && cfg.ui_style == 1) {
            glfwSetWindowOpacity(m_glfw_window, std::clamp(cfg.ui_opacity * 0.01f, 0.2f, 1.0f));
        }
    }

    DrawLabel("Radius:", 162, 108, 58, 20);
    if (DrawDraggableValueButton("btnBlurVal", "Radius (px)", 224, 106, 74, 22, sc,
                                cfg.bg_blur, 0, 64, 2, 18, "%d px", "Off")) {
        ConfigManager::SaveToFile(m_config_path, cfg);
    }

    // Row 3 (y=134): Soft Edges (18, 134, 136, 22) + Area Opacity (162, 134, 136, 22)
    if (DrawCheckbox("##chkSoftEdges", "Soft Edges", cfg.antialiasing, 18, 134, 136, 22)) {
        ConfigManager::SaveToFile(m_config_path, cfg);
    }
    bool area_op_bool = (cfg.ui_area_opacity != 0);
    if (DrawCheckbox("##chkAreaOpacity", "Area Opacity", area_op_bool, 162, 134, 136, 22)) {
        cfg.ui_area_opacity = area_op_bool ? 100 : 0;
        ConfigManager::SaveToFile(m_config_path, cfg);
    }

    // Row 4 (y=158): Outline (18, 158, 136, 22) + Outline Opacity (162, 158, 136, 22)
    if (DrawCheckbox("##chkOutline", "Outline", cfg.ui_outline, 18, 158, 136, 22)) {
        ConfigManager::SaveToFile(m_config_path, cfg);
    }
    bool outline_op_bool = (cfg.ui_outline_opacity != 0);
    if (DrawCheckbox("##chkOutlineOpacity", "Outline Opacity", outline_op_bool, 162, 158, 136, 22)) {
        cfg.ui_outline_opacity = outline_op_bool ? 100 : 0;
        ConfigManager::SaveToFile(m_config_path, cfg);
    }

    // Row 5 (y=186): + Image (18, 186, 136, 24) + X (162, 186, 136, 24)
    if (DrawButton("btnSetBg", "+ Image", 18, 186, 136, 24)) {
        OpenImageFileDialog();
    }
    if (DrawButton("btnClearBg", "X", 162, 186, 136, 24)) {
        SetBackgroundImage("");
    }

    // Row 6 (y=216): Copy to App (18, 216, 136, 22) + Delete Old (162, 216, 136, 22)
    if (DrawCheckbox("##chkCopyBg", "Copy to App", cfg.copy_bg_image, 18, 216, 136, 22)) {
        ConfigManager::SaveToFile(m_config_path, cfg);
    }
    if (DrawCheckbox("##chkDeleteOldBg", "Delete Old", cfg.delete_old_bg_image, 162, 216, 136, 22)) {
        ConfigManager::SaveToFile(m_config_path, cfg);
    }

    // Row 7 (y=242): Debug Mode (18, 242, 136, 22)
    if (DrawCheckbox("##chkDebugMode", "Debug Mode", cfg.debug_mode, 18, 242, 136, 22)) {
        PushLiveConfig();
        ConfigManager::SaveToFile(m_config_path, cfg);
    }
}

void MainWindow::RenderThemeControls(float sc) {
    auto S = [sc](int v) noexcept -> float { return std::round(v * sc); };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetWindowPos();

    OsuPointConfig& cfg = m_config;
    std::string custom_hex = "";
    if (cfg.palettes.count(cfg.palette)) custom_hex = cfg.palettes[cfg.palette].accent;

    auto DrawLabel = [&](const char* text, int x, int y, int w, int h) {
        (void)w;
        ImVec2 p0(win_pos.x + S(x), win_pos.y + S(y));
        ImVec2 txt_pos(p0.x, p0.y + (S(h) - ImGui::GetTextLineHeight()) * 0.5f);
        dl->AddText(txt_pos, IM_COL32(185, 185, 200, 255), text);
    };

    auto DrawButton = [&](const char* id, const char* label, int x, int y, int w, int h) -> bool {
        ImGui::SetCursorScreenPos(ImVec2(win_pos.x + S(x), win_pos.y + S(y)));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(30.0f/255.0f, 30.0f/255.0f, 36.0f/255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(42.0f/255.0f, 42.0f/255.0f, 52.0f/255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(48.0f/255.0f, 48.0f/255.0f, 60.0f/255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(52.0f/255.0f, 52.0f/255.0f, 62.0f/255.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(5));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

        char full_id[64];
        std::snprintf(full_id, sizeof(full_id), "%s##%s", label, id);
        bool clicked = ImGui::Button(full_id, ImVec2(S(w), S(h)));

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
        return clicked;
    };

    // Theme Subtab Controls (Exact Windows v0.6.1 Build 6 Coordinates)
    // Row 1 (y=80..82): Theme: (18, 82, 56, 20) + Combo (78, 80, 220, 24)
    DrawLabel("Theme:", 18, 82, 56, 20);
    ImGui::SetCursorScreenPos(ImVec2(win_pos.x + S(78), win_pos.y + S(80)));
    ImGui::SetNextItemWidth(S(220));

    const auto& builtin = Theme::GetBuiltinPalettes();
    if (ImGui::BeginCombo("##themeCbo", cfg.palette.c_str())) {
        for (const auto& pal : builtin) {
            bool is_sel = (pal == cfg.palette);
            ImVec4 pal_col = Theme::GetAccentColor(pal);
            ImGui::ColorButton("##pb", pal_col, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs, ImVec2(S(12), S(12)));
            ImGui::SameLine();
            if (ImGui::Selectable(pal.c_str(), is_sel)) {
                cfg.palette = pal;
                ConfigManager::SaveToFile(m_config_path, cfg);
                Theme::Apply(cfg.theme_base, cfg.palette, cfg.ui_opacity, custom_hex);
            }
        }
        for (const auto& [name, pal] : cfg.palettes) {
            bool is_sel = (name == cfg.palette);
            ImVec4 pal_col = Theme::HexToColor(pal.accent);
            ImGui::ColorButton("##cpb", pal_col, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs, ImVec2(S(12), S(12)));
            ImGui::SameLine();
            if (ImGui::Selectable(name.c_str(), is_sel)) {
                cfg.palette = name;
                ConfigManager::SaveToFile(m_config_path, cfg);
                Theme::Apply(cfg.theme_base, cfg.palette, cfg.ui_opacity, pal.accent);
            }
        }
        ImGui::EndCombo();
    }

    // Row 2 (y=116): Pick (18, 116, 90, 24) + Save (113, 116, 90, 24) + Del (208, 116, 90, 24)
    if (DrawButton("btnPickPalette", "Pick", 18, 116, 90, 24)) {
        ImGui::OpenPopup("PickColorPopup");
    }
    if (DrawButton("btnSaveTheme", "Save", 113, 116, 90, 24)) {
        PaletteConfig pc{};
        pc.name = "Custom";
        pc.accent = custom_hex.empty() ? "#0078D6" : custom_hex;
        cfg.palettes["Custom"] = pc;
        ConfigManager::SaveToFile(m_config_path, cfg);
    }
    if (DrawButton("btnDelTheme", "Del", 208, 116, 90, 24)) {
        if (cfg.palettes.count(cfg.palette)) {
            cfg.palettes.erase(cfg.palette);
            cfg.palette = "system";
            ConfigManager::SaveToFile(m_config_path, cfg);
            Theme::Apply("dark", cfg.palette, cfg.ui_opacity, "");
        }
    }

    if (ImGui::BeginPopup("PickColorPopup")) {
        ImVec4 cur_col = Theme::GetAccentColor(cfg.palette, custom_hex);
        float col_arr[3] = { cur_col.x, cur_col.y, cur_col.z };
        if (ImGui::ColorPicker3("##pickAccent", col_arr, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview)) {
            char hex_buf[16];
            std::snprintf(hex_buf, sizeof(hex_buf), "#%02X%02X%02X",
                          static_cast<int>(col_arr[0] * 255.0f),
                          static_cast<int>(col_arr[1] * 255.0f),
                          static_cast<int>(col_arr[2] * 255.0f));
            PaletteConfig pc{};
            pc.name = "Custom";
            pc.accent = hex_buf;
            cfg.palettes["Custom"] = pc;
            cfg.palette = "Custom";
            ConfigManager::SaveToFile(m_config_path, cfg);
            Theme::Apply(cfg.theme_base, cfg.palette, cfg.ui_opacity, hex_buf);
        }
        ImGui::EndPopup();
    }
}

void MainWindow::RenderMiscControls(float sc) {
    auto S = [sc](int v) noexcept -> float { return std::round(v * sc); };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetWindowPos();

    OsuPointConfig& cfg = m_config;
    std::string custom_hex = "";
    if (cfg.palettes.count(cfg.palette)) custom_hex = cfg.palettes[cfg.palette].accent;
    ImVec4 accent_vec = Theme::GetAccentColor(cfg.palette, custom_hex);
    ImU32 col_accent = ImGui::GetColorU32(accent_vec);

    auto DrawButton = [&](const char* id, const char* label, int x, int y, int w, int h) -> bool {
        ImGui::SetCursorScreenPos(ImVec2(win_pos.x + S(x), win_pos.y + S(y)));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(30.0f/255.0f, 30.0f/255.0f, 36.0f/255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(42.0f/255.0f, 42.0f/255.0f, 52.0f/255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(48.0f/255.0f, 48.0f/255.0f, 60.0f/255.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(52.0f/255.0f, 52.0f/255.0f, 62.0f/255.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, S(5));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);

        char full_id[64];
        std::snprintf(full_id, sizeof(full_id), "%s##%s", label, id);
        bool clicked = ImGui::Button(full_id, ImVec2(S(w), S(h)));

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(4);
        return clicked;
    };

    auto DrawCheckbox = [&](const char* id, const char* label, bool& val, int x, int y, int w, int h) -> bool {
        ImVec2 p0(win_pos.x + S(x), win_pos.y + S(y));
        ImGui::SetCursorScreenPos(p0);
        bool clicked = ImGui::InvisibleButton(id, ImVec2(S(w), S(h)));
        if (clicked) val = !val;

        bool hovered = ImGui::IsItemHovered();
        float box_sz = S(14);
        float box_y = p0.y + (S(h) - box_sz) * 0.5f;

        ImU32 box_bg = val ? col_accent : (hovered ? IM_COL32(42, 42, 52, 255) : IM_COL32(30, 30, 36, 255));
        ImU32 box_border = val ? col_accent : IM_COL32(52, 52, 62, 255);

        dl->AddRectFilled(ImVec2(p0.x, box_y), ImVec2(p0.x + box_sz, box_y + box_sz), box_bg, S(4));
        dl->AddRect(ImVec2(p0.x, box_y), ImVec2(p0.x + box_sz, box_y + box_sz), box_border, S(4), 0, 1.0f);

        if (val) {
            ImVec2 chk_pts[3] = {
                ImVec2(p0.x + box_sz * 0.22f, box_y + box_sz * 0.50f),
                ImVec2(p0.x + box_sz * 0.44f, box_y + box_sz * 0.72f),
                ImVec2(p0.x + box_sz * 0.78f, box_y + box_sz * 0.28f)
            };
            dl->AddPolyline(chk_pts, 3, IM_COL32(255, 255, 255, 255), 0, S(2));
        }

        ImVec2 txt_pos(p0.x + box_sz + S(6), p0.y + (S(h) - ImGui::GetTextLineHeight()) * 0.5f);
        dl->AddText(txt_pos, IM_COL32(245, 245, 250, 255), label);
        return clicked;
    };

    // Misc Tab Controls (Exact Windows v0.6.1 Build 6 Coordinates)
    // Row 1 (y=48..49): Help (18, 48, 136, 24) + Unlock Limits (162, 49, 136, 22)
    if (DrawButton("btnHelp", "osu!Point - Manual", 18, 48, 136, 24)) {
        m_show_manual = true;
    }
    if (DrawCheckbox("##chkUnlockLimits", "Unlock Limits", cfg.unlock_limits, 162, 49, 136, 22)) {
        PushLiveConfig();
        ConfigManager::SaveToFile(m_config_path, cfg);
    }
}

void MainWindow::RenderManualWindowContent() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("osuPointManualContent", nullptr, flags)) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.7f, 1.0f), "osu!Point - Complete Technical Manual");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::BeginChild("ManualScrollRegion", ImVec2(0, -36), true)) {
            static const char* const entries[] = {
                "--- AREA MAPPING ---",
                "Width / Height: Tablet active area dimensions in millimeters (mm).",
                "Center X / Center Y: Offset of the area center from tablet origin (mm).",
                "Rotation: Clockwise rotation angle in degrees (0 - 360).",
                "Lock Aspect Ratio: Forces area height to maintain monitor aspect ratio.",
                "Auto Aspect Ratio: Automatically calculates height based on current width.",
                "Set to Full Area: Resets area dimensions to the maximum tablet surface.",
                "Lock Drag: Prevents accidental dragging or resizing in the visual area preview.",
                "Clip Area: Clamps cursor coordinates strictly within the mapped area.",
                "Pen Click Toggle: Enables or disables tablet pen tip click events.",
                "",
                "--- PEN & MOUSE DYNAMICS ---",
                "Soft Edges: Smooths pen cursor boundary transitions near area edges.",
                "Debug Mode: Renders real-time telemetry, raw input coords, and latency meters in a separate window.",
                "Outline Only: Disables solid area fills and renders modern thin borders.",
                "Outline Opacity: Adjusts border opacity level (10% - 100%).",
                "Area Opacity: Adjusts area interior fill opacity (10% - 100%).",
                "",
                "--- LATENCY, EXTRAPOLATION & SMOOTHING ---",
                "Extrapolate Lead: Predictive kinematic lead in mm to counter display latency.",
                "Anti-Noise: High-frequency jitter reduction filter (0% - 100%).",
                "Motion Filter: Dynamic adaptive smoothing curve for fast movements (0% - 100%).",
                "Low Latency Spin: Multi-threaded high-priority spin-wait polling (0% - 100%).",
                "",
                "--- APPEARANCE & THEMES ---",
                "UI Style: Switch between Normal (Classic Solid) and Acrylic (Fluent Blur).",
                "Blur Type: Choose background blur algorithm: Gaussian, Mosaic, Radial, Acrylic.",
                "Theme: Select predefined palettes or create and save custom themes.",
                "Card / Wallpaper Opacity & Blur: Fine-tune glass opacity and background blur.",
                "",
                "--- ADVANCED CONTROLS & UNLOCK LIMITS ---",
                "Unlock Limits: Removes safety clamping on area size, offsets, and filters.",
                "Drag Values: Hold left mouse button on value buttons and drag left/right to adjust.",
                "Right-Click Values: Right-click any value button for precise keyboard input."
            };

            for (const char* line : entries) {
                if (!line || line[0] == '\0') {
                    ImGui::Spacing();
                    continue;
                }
                if (line[0] == '-' && line[1] == '-' && line[2] == '-') {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.8f, 1.0f), "%s", line);
                    ImGui::Separator();
                } else {
                    ImGui::BulletText("%s", line);
                }
            }
        }
        ImGui::EndChild();

        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(100, 26))) {
            m_show_manual = false;
        }
    }
    ImGui::End();
}

void MainWindow::CopyDiagnosticsToClipboard() {
    char buf[2048];
    const char* dev_name = m_telemetry.device_name[0] ? m_telemetry.device_name : "Tablet";
    char hex_dump[128] = {0};
    int hpos = 0;
    for (uint8_t i = 0; i < std::min(m_debug_snapshot.packet_size, uint8_t(16)); ++i) {
        hpos += std::snprintf(hex_dump + hpos, sizeof(hex_dump) - hpos, "%02X ", m_debug_snapshot.packet[i]);
    }

    std::snprintf(buf, sizeof(buf),
        "=== osu!Point Linux Telemetry & Diagnostics ===\n"
        "Device: %s (VID: 0x%04X, PID: 0x%04X)\n"
        "Physical: %.1f x %.1f mm | Max Raw: %d x %d | Pressure Levels: %u\n"
        "HID Path: %s\n\n"
        "--- Telemetry Metrics ---\n"
        "Rate: %.1f Hz (Output: %.1f Hz)\n"
        "Latency: p50=%.1fus, p95=%.1fus, p99=%.1fus, max=%.1fus\n"
        "Jitter: \xC2\xB1%.1fus (stddev=%.1fus, rfc=%.1fus)\n"
        "Packets: total=%llu, write_err=%llu, read_err=%llu, reconnects=%llu\n"
        "Decodes: ok=%llu, unk_id=%llu, trunc=%llu, inv_coord=%llu, lift=%llu, zero=%llu\n"
        "Last Raw Packet [%u bytes]: %s\n\n"
        "--- Tablet Mapping ---\n"
        "Area: %.1f x %.1f mm at (%.1f, %.1f) | Rotation: %.1f deg\n"
        "Flags: clip=%d, lock_drag=%d, lock_ratio=%d, ratio=%s\n"
        "Filters: extrap=%.1fmm, anti_noise=%d%%, motion_filter=%d%%, spin=%d%%\n\n"
        "--- RT Health ---\n"
        "TID: %d | Policy: %d | Prio: %d | Nice: %d | Core: %d\n"
        "Virtual Device: %s (%dx%d) | EvdevGrab: %d | Mlock: %d\n"
        "================================================",
        dev_name, m_telemetry.vendor_id, m_telemetry.product_id,
        m_telemetry.phys_w_mm, m_telemetry.phys_h_mm, m_telemetry.max_x, m_telemetry.max_y,
        m_config.pressure_max ? m_config.pressure_max : 2048,
        m_telemetry.hidraw_path[0] ? m_telemetry.hidraw_path : "N/A",
        m_telemetry.current_hz, m_debug_snapshot.output_hz,
        m_telemetry.latency_p50_us, m_debug_snapshot.latency_p95_us, m_telemetry.latency_p99_us, m_telemetry.latency_max_us,
        m_telemetry.jitter_us, m_telemetry.interval_stddev_us, m_telemetry.jitter_rfc_us,
        static_cast<unsigned long long>(m_telemetry.total_reports),
        static_cast<unsigned long long>(m_debug_snapshot.write_errors),
        static_cast<unsigned long long>(m_debug_snapshot.read_errors),
        static_cast<unsigned long long>(m_debug_snapshot.reconnects),
        static_cast<unsigned long long>(m_debug_snapshot.decode_reasons[0]),
        static_cast<unsigned long long>(m_debug_snapshot.decode_reasons[1]),
        static_cast<unsigned long long>(m_debug_snapshot.decode_reasons[2]),
        static_cast<unsigned long long>(m_debug_snapshot.decode_reasons[3]),
        static_cast<unsigned long long>(m_debug_snapshot.decode_reasons[4]),
        static_cast<unsigned long long>(m_debug_snapshot.decode_reasons[5]),
        m_debug_snapshot.packet_size, hex_dump,
        m_width_mm, m_height_mm, m_cx_mm, m_cy_mm, m_rotation_deg,
        m_clip_area ? 1 : 0, m_lock_drag ? 1 : 0, m_lock_ratio ? 1 : 0, m_config.ratio.c_str(),
        m_config.extrapolation_mm, m_config.filter, m_config.motion_filter, m_config.spin_percent,
        m_telemetry.pipeline_tid, m_telemetry.scheduler_policy, m_telemetry.scheduler_priority,
        m_telemetry.nice_value, m_telemetry.requested_core,
        m_telemetry.output_name[0] ? m_telemetry.output_name : "N/A",
        m_telemetry.output_resolution_x, m_telemetry.output_resolution_y,
        m_telemetry.evdev_grabbed ? 1 : 0, m_telemetry.memory_locked ? 1 : 0);

    ImGui::SetClipboardText(buf);
}

void MainWindow::RenderDebugWindowContent() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("osuPointDebugContent", nullptr, flags)) {
        const bool connected = m_telemetry.connected != 0;
        const char* dev_name = m_telemetry.device_name[0] ? m_telemetry.device_name : "Tablet";

        // Header Title & Connection Badge
        ImGui::TextColored(ImVec4(0.2f, 0.85f, 1.0f, 1.0f), "osu!Point Telemetry & Diagnostics");
        ImGui::SameLine();
        if (connected) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[CONNECTED]");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "[WAITING FOR TABLET]");
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.3f, 0.95f, 0.5f, 1.0f), "[ACTIVE]");
        ImGui::Separator();

        // 1. Device Hardware Specs
        ImGui::Text("Device:      %s (VID: 0x%04X, PID: 0x%04X)", dev_name, m_telemetry.vendor_id, m_telemetry.product_id);
        ImGui::Text("Physical:    %.1f x %.1f mm | Max Raw: %d x %d | Pressure Levels: %u",
                    m_telemetry.phys_w_mm, m_telemetry.phys_h_mm,
                    m_telemetry.max_x, m_telemetry.max_y,
                    m_config.pressure_max ? m_config.pressure_max : 2048);
        ImGui::Text("Path:        %s", m_telemetry.hidraw_path[0] ? m_telemetry.hidraw_path : "N/A");
        ImGui::Spacing();
        ImGui::Separator();

        // 2. Metrics 4-Box Grid (Rate, Latency, Jitter, Packets)
        float avail_w = ImGui::GetContentRegionAvail().x;
        float card_w = (avail_w - 30.0f) * 0.25f;
        float card_h = 68.0f;

        // Card 1: Polling Rate
        ImGui::BeginChild("##card_rate", ImVec2(card_w, card_h), true);
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.8f, 1.0f), "POLLING RATE");
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f), "%.0f Hz", m_telemetry.current_hz);
        ImGui::TextDisabled("Output: %.0f Hz", m_debug_snapshot.output_hz);
        ImGui::EndChild();
        ImGui::SameLine();

        // Card 2: Latency
        ImGui::BeginChild("##card_lat", ImVec2(card_w, card_h), true);
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.8f, 1.0f), "LATENCY (p50)");
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "%.1f us", m_telemetry.latency_p50_us);
        ImGui::TextDisabled("p99: %.0fus | max: %.0fus", m_telemetry.latency_p99_us, m_telemetry.latency_max_us);
        ImGui::EndChild();
        ImGui::SameLine();

        // Card 3: Jitter
        ImGui::BeginChild("##card_jit", ImVec2(card_w, card_h), true);
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.8f, 1.0f), "JITTER (RFC)");
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1.0f), "\xC2\xB1%.1f us", m_telemetry.jitter_us);
        ImGui::TextDisabled("stddev: %.1fus", m_telemetry.interval_stddev_us);
        ImGui::EndChild();
        ImGui::SameLine();

        // Card 4: Reports / Packet Quality
        ImGui::BeginChild("##card_packets", ImVec2(card_w, card_h), true);
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.8f, 1.0f), "PACKETS");
        ImGui::Text("%llu ok", static_cast<unsigned long long>(m_telemetry.total_reports));
        ImGui::TextDisabled("rej/err: %llu", static_cast<unsigned long long>(m_telemetry.dropped_reports));
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Separator();

        // 3. Live Stylus Telemetry & Pressure Meter
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.8f, 1.0f), "Live Stylus State & Dynamics:");
        int32_t raw_x = m_debug_snapshot.raw_x;
        int32_t raw_y = m_debug_snapshot.raw_y;
        int32_t out_x = m_debug_snapshot.out_x;
        int32_t out_y = m_debug_snapshot.out_y;
        uint16_t pressure = m_debug_snapshot.pressure;
        uint16_t max_p = m_config.pressure_max ? m_config.pressure_max : 2048;
        bool in_range = (m_debug_snapshot.flags & PEN_FLAG_IN_RANGE) != 0;
        bool tip_down = (m_debug_snapshot.flags & PEN_FLAG_PRESSED) != 0;
        bool barrel_down = (m_debug_snapshot.flags & PEN_FLAG_BARREL) != 0;

        // Instantaneous pen velocity calculation from recent trace
        float speed_mms = 0.0f;
        if (m_debug_snapshot.trace_count >= 2) {
            const auto& p1 = m_debug_snapshot.trace[m_debug_snapshot.trace_count - 1];
            const auto& p2 = m_debug_snapshot.trace[m_debug_snapshot.trace_count - 2];
            float dx = p1.filtered_x_mm - p2.filtered_x_mm;
            float dy = p1.filtered_y_mm - p2.filtered_y_mm;
            float dist_mm = std::sqrt(dx * dx + dy * dy);
            float intv_s = m_debug_snapshot.interval_p50_us > 0.0 ? static_cast<float>(m_debug_snapshot.interval_p50_us * 1e-6) : 0.0075f;
            if (intv_s > 0.0f) speed_mms = dist_mm / intv_s;
        }

        ImGui::Text("Raw Coords:  X: %5d  Y: %5d", raw_x, raw_y);
        ImGui::Text("Screen Pos:  X: %5d  Y: %5d (%s mode) | Speed: %.1f mm/s",
                    out_x, out_y, m_config.mouse_mode ? "Mouse" : "Stylus", speed_mms);
        ImGui::Text("Buttons:     Tip: %s | Barrel: %s | State: %s",
                    tip_down ? "[DOWN]" : "UP",
                    barrel_down ? "[PRESSED]" : "RELEASED",
                    in_range ? "IN RANGE" : "OUT OF RANGE");

        // Visual animated pressure bar
        float p_fraction = std::clamp(static_cast<float>(pressure) / static_cast<float>(max_p), 0.0f, 1.0f);
        char p_buf[64];
        std::snprintf(p_buf, sizeof(p_buf), "Pressure: %u / %u (%.1f%%)", pressure, max_p, p_fraction * 100.0f);
        ImGui::ProgressBar(p_fraction, ImVec2(-1, 20), p_buf);

        ImGui::Spacing();
        ImGui::Separator();

        // 4. Raw Packet Hex Dump & Decode Reasons
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.8f, 1.0f), "Raw HID Packet Inspector (%u bytes):", m_debug_snapshot.packet_size);
        char hex_buf[256] = {0};
        int hpos = 0;
        for (uint8_t b = 0; b < std::min(m_debug_snapshot.packet_size, uint8_t(16)); ++b) {
            hpos += std::snprintf(hex_buf + hpos, sizeof(hex_buf) - hpos, "%02X ", m_debug_snapshot.packet[b]);
        }
        if (m_debug_snapshot.packet_size > 16) {
            std::snprintf(hex_buf + hpos, sizeof(hex_buf) - hpos, "...");
        }
        ImGui::TextDisabled("HEX: %s", hex_buf[0] ? hex_buf : "[Empty / No Packet]");

        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.8f, 1.0f), "Decode Reason Breakdown:");
        ImGui::Text("Ok: %llu | UnknownId: %llu | Trunc: %llu | InvCoord: %llu | Lift: %llu | Zero: %llu",
                    static_cast<unsigned long long>(m_debug_snapshot.decode_reasons[0]),
                    static_cast<unsigned long long>(m_debug_snapshot.decode_reasons[1]),
                    static_cast<unsigned long long>(m_debug_snapshot.decode_reasons[2]),
                    static_cast<unsigned long long>(m_debug_snapshot.decode_reasons[3]),
                    static_cast<unsigned long long>(m_debug_snapshot.decode_reasons[4]),
                    static_cast<unsigned long long>(m_debug_snapshot.decode_reasons[5]));

        ImGui::Spacing();
        ImGui::Separator();

        // 5. Area Mapping & Active Filters
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.8f, 1.0f), "Mapping & Pipeline Configuration:");
        ImGui::Text("Area Bounds: %.1f x %.1f mm at (%.1f, %.1f) | Rot: %.1f deg",
                    m_width_mm, m_height_mm, m_cx_mm, m_cy_mm, m_rotation_deg);
        ImGui::Text("Area Flags:  Clip: %s | Lock Drag: %s | Ratio: %s (%s)",
                    m_clip_area ? "ON" : "OFF",
                    m_lock_drag ? "ON" : "OFF",
                    m_config.ratio.c_str(),
                    m_lock_ratio ? "Locked" : "Unlocked");
        ImGui::Text("Smoothing:   Extrap Lead: %.1f mm | Anti-Noise: %d%% | Motion Filter: %d%% | Spin: %d%%",
                    m_config.extrapolation_mm, m_config.filter, m_config.motion_filter, m_config.spin_percent);

        ImGui::Spacing();
        ImGui::Separator();

        // 6. System RT & Driver Health
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.8f, 1.0f), "Driver Runtime Health:");
        ImGui::Text("Pipeline:    TID: %d | Policy: %s (prio %d) | Nice: %d | Core: %d",
                    m_telemetry.pipeline_tid,
                    m_telemetry.scheduler_policy == SCHED_FIFO ? "SCHED_FIFO" :
                    (m_telemetry.scheduler_policy == SCHED_RR ? "SCHED_RR" : "SCHED_OTHER"),
                    m_telemetry.scheduler_priority, m_telemetry.nice_value,
                    m_telemetry.requested_core);
        ImGui::Text("Virtual Dev: %s (%dx%d) | Grabbed: %s | MemLocked: %s",
                    m_telemetry.output_name[0] ? m_telemetry.output_name : "N/A",
                    m_telemetry.output_resolution_x, m_telemetry.output_resolution_y,
                    m_telemetry.evdev_grabbed ? "YES" : "NO",
                    m_telemetry.memory_locked ? "YES" : "NO");

        ImGui::Spacing();
        ImGui::Separator();

        // 7. Recent Trajectory Trail
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.8f, 1.0f), "Recent Filter Trajectory (mm):");
        if (ImGui::BeginTable("##trajTable", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp, ImVec2(0, 100))) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30);
            ImGui::TableSetupColumn("Raw (X, Y) mm", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Filtered (X, Y) mm", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Delta", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableHeadersRow();

            for (uint32_t i = 0; i < std::min(m_debug_snapshot.trace_count, 6u); ++i) {
                const auto& pt = m_debug_snapshot.trace[i];
                float dx = pt.filtered_x_mm - pt.raw_x_mm;
                float dy = pt.filtered_y_mm - pt.raw_y_mm;
                float delta_mm = std::sqrt(dx * dx + dy * dy);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%u", i + 1);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.2f, %.2f", pt.raw_x_mm, pt.raw_y_mm);
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.2f, %.2f", pt.filtered_x_mm, pt.filtered_y_mm);
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.3f mm", delta_mm);
            }
            ImGui::EndTable();
        }

        ImGui::Spacing();
        if (ImGui::Button("Copy Diagnostics to Clipboard", ImVec2(220, 26))) {
            CopyDiagnosticsToClipboard();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Counters", ImVec2(130, 26))) {
            m_debug_snapshot = {};
            m_telemetry.latency_max_us = 0;
            m_telemetry.dropped_reports = 0;
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(90, 26))) {
            SetDebugOpen(false);
        }
    }
    ImGui::End();
}

void MainWindow::RenderTabletPreview(float sc) {
    auto S = [sc](int v) noexcept -> float { return std::round(v * sc); };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 win_pos = ImGui::GetWindowPos();

    OsuPointConfig& cfg = m_config;
    std::string custom_hex = "";
    if (cfg.palettes.count(cfg.palette)) custom_hex = cfg.palettes[cfg.palette].accent;
    ImVec4 accent_vec = Theme::GetAccentColor(cfg.palette, custom_hex);
    ImU32 col_accent = ImGui::GetColorU32(accent_vec);

    // Exact Upstream Preview Dimensions
    float scaled_preview_x = S(PREVIEW_X);
    float scaled_preview_y = S(PREVIEW_Y);
    float scaled_preview_max_w = S(PREVIEW_MAX_W);
    float scaled_preview_max_h = S(PREVIEW_MAX_H);
    float scaled_bezel = S(BEZEL);

    float active_max_w = scaled_preview_max_w - 2.0f * scaled_bezel;
    float active_max_h = scaled_preview_max_h - 2.0f * scaled_bezel;

    double pw = (m_phys_w_mm > 0.0) ? m_phys_w_mm : 152.0;
    double ph = (m_phys_h_mm > 0.0) ? m_phys_h_mm : 95.0;

    double scaleX = active_max_w / pw;
    double scaleY = active_max_h / ph;
    double s = std::min(scaleX, scaleY);

    float active_w = static_cast<float>(std::round(pw * s));
    float active_h = static_cast<float>(std::round(ph * s));

    float offset_x = (active_max_w - active_w) * 0.5f;
    float offset_y = (active_max_h - active_h) * 0.5f;

    float body_x = win_pos.x + scaled_preview_x + offset_x;
    float body_y = win_pos.y + scaled_preview_y + offset_y;
    float body_w = active_w + 2.0f * scaled_bezel;
    float body_h = active_h + 2.0f * scaled_bezel;

    // 1. Tablet Bezel (#121216, border #282830, rounding 8)
    dl->AddRectFilled(ImVec2(body_x, body_y), ImVec2(body_x + body_w, body_y + body_h),
                      IM_COL32(18, 18, 22, 255), S(8));
    dl->AddRect(ImVec2(body_x, body_y), ImVec2(body_x + body_w, body_y + body_h),
                IM_COL32(40, 40, 48, 255), S(8), 0, 1.0f);

    float act_left = body_x + scaled_bezel;
    float act_top = body_y + scaled_bezel;

    // 2. Active Surface (#0C0C10, border #1E1E24)
    dl->AddRectFilled(ImVec2(act_left, act_top), ImVec2(act_left + active_w, act_top + active_h),
                      IM_COL32(12, 12, 16, 255), S(4));
    dl->AddRect(ImVec2(act_left, act_top), ImVec2(act_left + active_w, act_top + active_h),
                IM_COL32(30, 30, 36, 255), S(4), 0, 1.0f);

    // Grid lines (50mm intervals, #1C1C24)
    ImU32 col_grid = IM_COL32(28, 28, 36, 255);
    for (double gx = 50.0; gx < pw; gx += 50.0) {
        float x = static_cast<float>(act_left + gx * s);
        dl->AddLine(ImVec2(x, act_top), ImVec2(x, act_top + active_h), col_grid, 1.0f);
    }
    for (double gy = 50.0; gy < ph; gy += 50.0) {
        float y = static_cast<float>(act_top + gy * s);
        dl->AddLine(ImVec2(act_left, y), ImVec2(act_left + active_w, y), col_grid, 1.0f);
    }

    // 3. Rotated Polygon Calculations
    double rad = m_rotation_deg * (M_PI / 180.0);
    double cos_val = std::cos(rad);
    double sin_val = std::sin(rad);

    double hw = m_width_mm * 0.5;
    double hh = m_height_mm * 0.5;

    double corners_x[4] = { -hw,  hw,  hw, -hw };
    double corners_y[4] = { -hh, -hh,  hh,  hh };

    const bool aa = m_config.antialiasing;
    ImVec2 polyPts[4];
    for (int i = 0; i < 4; ++i) {
        double rx = corners_x[i] * cos_val - corners_y[i] * sin_val;
        double ry = corners_x[i] * sin_val + corners_y[i] * cos_val;
        float fx = static_cast<float>(act_left + (m_cx_mm + rx) * s);
        float fy = static_cast<float>(act_top + (m_cy_mm + ry) * s);
        if (aa) {
            polyPts[i] = ImVec2(fx, fy);
        } else {
            polyPts[i] = ImVec2(std::round(fx), std::round(fy));
        }
    }
    ImVec2 centerPt;
    if (aa) {
        centerPt = ImVec2(static_cast<float>(act_left + m_cx_mm * s),
                          static_cast<float>(act_top + m_cy_mm * s));
    } else {
        centerPt = ImVec2(static_cast<float>(std::round(act_left + m_cx_mm * s)),
                          static_cast<float>(std::round(act_top + m_cy_mm * s)));
    }

    const ImDrawListFlags saved_flags = dl->Flags;
    if (aa) {
        dl->Flags |= (ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill);
    } else {
        dl->Flags &= ~(ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill);
    }

    // Active Area Fill (accent color with alpha 55 = 0.22)
    ImVec4 fill_vec = accent_vec;
    fill_vec.w = 55.0f / 255.0f;
    dl->AddConvexPolyFilled(polyPts, 4, ImGui::GetColorU32(fill_vec));

    // Outline (pen width 1.6f)
    dl->AddPolyline(polyPts, 4, col_accent, ImDrawFlags_Closed, 1.6f);

    // Center Crosshair (length 6px)
    float cl = 6.0f;
    dl->AddLine(ImVec2(centerPt.x - cl, centerPt.y), ImVec2(centerPt.x + cl, centerPt.y), col_accent, 1.5f);
    dl->AddLine(ImVec2(centerPt.x, centerPt.y - cl), ImVec2(centerPt.x, centerPt.y + cl), col_accent, 1.5f);

    dl->Flags = saved_flags;

    // Corner Handles (white circular dots, radius 3.5px, accent border)
    if (!m_lock_drag) {
        float hr = std::max(3.0f, static_cast<float>(std::round(3.5f * sc)));
        for (int i = 0; i < 4; ++i) {
            dl->AddCircleFilled(polyPts[i], hr, IM_COL32(245, 245, 250, 255), 16);
            dl->AddCircle(polyPts[i], hr, col_accent, 16, 1.0f);
        }
    }

    // --- Interactive Mouse Dragging (Hit-Testing) ---
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse = io.MousePos;

    if (!m_lock_drag) {
        auto HitTest = [&]() -> int {
            float hr = 6.0f * sc;
            for (int i = 0; i < 4; ++i) {
                float dx = mouse.x - polyPts[i].x;
                float dy = mouse.y - polyPts[i].y;
                if ((dx * dx + dy * dy) <= (hr * hr)) {
                    return i + 1; // 1..4 = corner handles
                }
            }
            // Point inside rotated polygon test (ray casting)
            bool inside = false;
            for (int i = 0, j = 3; i < 4; j = i++) {
                if (((polyPts[i].y > mouse.y) != (polyPts[j].y > mouse.y)) &&
                    (mouse.x < (polyPts[j].x - polyPts[i].x) * (mouse.y - polyPts[i].y) /
                               (polyPts[j].y - polyPts[i].y) + polyPts[i].x)) {
                    inside = !inside;
                }
            }
            return inside ? 5 : -1;
        };

        if (m_drag_mode == DragMode::None) {
            if (io.MouseClicked[0]) {
                int hit = HitTest();
                if (hit == 5) {
                    m_drag_mode = DragMode::Move;
                    m_drag_start_mouse = mouse;
                    m_drag_orig_cx = m_cx_mm;
                    m_drag_orig_cy = m_cy_mm;
                } else if (hit >= 1 && hit <= 4) {
                    m_drag_mode = static_cast<DragMode>(hit + 1); // Corner0..3
                    m_drag_start_mouse = mouse;
                    m_drag_orig_w = m_width_mm;
                    m_drag_orig_h = m_height_mm;
                    m_drag_orig_cx = m_cx_mm;
                    m_drag_orig_cy = m_cy_mm;
                }
            }
        } else {
            if (io.MouseDown[0]) {
                if (m_drag_mode == DragMode::Move) {
                    double dx_px = mouse.x - m_drag_start_mouse.x;
                    double dy_px = mouse.y - m_drag_start_mouse.y;
                    double dx_mm = (s > 1e-6) ? (dx_px / s) : 0.0;
                    double dy_mm = (s > 1e-6) ? (dy_px / s) : 0.0;

                    m_cx_mm = std::clamp(m_drag_orig_cx + dx_mm, 0.0, pw);
                    m_cy_mm = std::clamp(m_drag_orig_cy + dy_mm, 0.0, ph);
                    std::snprintf(m_edit_cx, sizeof(m_edit_cx), "%s", FormatDouble(m_cx_mm).c_str());
                    std::snprintf(m_edit_cy, sizeof(m_edit_cy), "%s", FormatDouble(m_cy_mm).c_str());
                    m_dirty_config = true;
                } else {
                    // Corner resize
                    double dx_px = mouse.x - centerPt.x;
                    double dy_px = mouse.y - centerPt.y;
                    double unrot_x = (dx_px * cos_val + dy_px * sin_val) / s;
                    double unrot_y = (-dx_px * sin_val + dy_px * cos_val) / s;

                    double nw = std::clamp(std::abs(unrot_x) * 2.0, 1.0, pw);
                    double nh = std::clamp(std::abs(unrot_y) * 2.0, 1.0, ph);

                    if (m_lock_ratio) {
                        double r = ConfigManager::SafeStod(m_edit_ratio, 16.0 / 9.0);
                        if (r > 1e-4) {
                            nh = std::clamp(nw / r, 1.0, ph);
                            nw = nh * r;
                        }
                    }

                    m_width_mm = nw;
                    m_height_mm = nh;
                    std::snprintf(m_edit_w, sizeof(m_edit_w), "%s", FormatDouble(m_width_mm).c_str());
                    std::snprintf(m_edit_h, sizeof(m_edit_h), "%s", FormatDouble(m_height_mm).c_str());
                    m_dirty_config = true;
                }
            } else {
                m_drag_mode = DragMode::None;
            }
        }
    }
}

void MainWindow::SetBackgroundImage(const std::string& path) {
    if (path.empty()) {
        m_config.bg_image.clear();
        ConfigManager::SaveToFile(m_config_path, m_config);
        ReloadBackgroundTexture();
        MarkDirty();
        return;
    }

    std::string final_path = path;
    if (m_config.copy_bg_image) {
        try {
            std::filesystem::path src(path);
            if (std::filesystem::exists(src)) {
                std::string fname = src.filename().string();
                std::filesystem::path dst = std::filesystem::current_path() / fname;
                if (std::filesystem::absolute(src) != std::filesystem::absolute(dst)) {
                    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing);
                }
                final_path = fname;
                if (m_config.delete_old_bg_image && !m_config.copied_bg_images.empty() && m_config.copied_bg_images != fname) {
                    std::filesystem::path old_dst = std::filesystem::current_path() / m_config.copied_bg_images;
                    if (std::filesystem::exists(old_dst)) {
                        std::filesystem::remove(old_dst);
                    }
                }
                m_config.copied_bg_images = fname;
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[osuPoint] Error copying background image: %s\n", e.what());
        }
    }

    m_config.bg_image = final_path;
    ConfigManager::SaveToFile(m_config_path, m_config);
    ReloadBackgroundTexture();
    MarkDirty();
}

void MainWindow::ReloadBackgroundTexture() {
    if (m_config.bg_image.empty()) {
        if (m_bg_texture != 0) {
            glDeleteTextures(1, &m_bg_texture);
            m_bg_texture = 0;
        }
        m_bg_tex_w = 0;
        m_bg_tex_h = 0;
        m_loaded_bg_path.clear();
        return;
    }

    std::filesystem::path p(m_config.bg_image);
    if (!std::filesystem::exists(p)) {
        std::filesystem::path cfg_dir = std::filesystem::path(m_config_path).parent_path();
        if (!cfg_dir.empty() && std::filesystem::exists(cfg_dir / p)) {
            p = cfg_dir / p;
        }
    }

    int w = 0, h = 0, comp = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* data = stbi_load(p.string().c_str(), &w, &h, &comp, 4);
    if (!data) {
        std::fprintf(stderr, "[osuPoint] Failed to load background image: %s\n", p.string().c_str());
        if (m_bg_texture != 0) {
            glDeleteTextures(1, &m_bg_texture);
            m_bg_texture = 0;
        }
        m_bg_tex_w = 0;
        m_bg_tex_h = 0;
        m_loaded_bg_path.clear();
        return;
    }

    if (m_bg_texture == 0) {
        glGenTextures(1, &m_bg_texture);
    }
    glBindTexture(GL_TEXTURE_2D, m_bg_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(data);
    m_bg_tex_w = w;
    m_bg_tex_h = h;
    m_loaded_bg_path = p.string();
}

void MainWindow::OpenImageFileDialog() {
    std::thread([this]() {
        FILE* fp = popen("zenity --file-selection --title=\"Select Background Image\" --file-filter=\"Image Files | *.png *.jpg *.jpeg *.bmp *.webp *.PNG *.JPG *.JPEG *.BMP *.WEBP\" --file-filter=\"All Files | *\" 2>/dev/null", "r");
        if (fp) {
            char path_buf[1024];
            if (std::fgets(path_buf, sizeof(path_buf), fp)) {
                size_t len = std::strlen(path_buf);
                while (len > 0 && (path_buf[len - 1] == '\n' || path_buf[len - 1] == '\r')) {
                    path_buf[len - 1] = '\0';
                    --len;
                }
                if (len > 0) {
                    {
                        std::lock_guard<std::mutex> lock(m_bg_mutex);
                        m_pending_bg_path = path_buf;
                    }
                    if (m_glfw_window) {
                        glfwPostEmptyEvent();
                    }
                }
            }
            pclose(fp);
        }
    }).detach();
}

} // namespace osupoint::gui
