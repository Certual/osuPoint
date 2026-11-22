#pragma once

#include "osupoint/config_manager.h"
#include "osupoint/driver_service.h"
#include "osupoint/shm_client.h"
#include "theme.h"
#include <string>
#include <chrono>
#include <mutex>

struct GLFWwindow;

namespace osupoint::gui {

class MainWindow {
public:
    MainWindow(const std::string& config_path = "config.ini", GLFWwindow* glfw_win = nullptr);
    ~MainWindow();

    void Render();
    bool ShouldClose() const noexcept { return m_should_close; }
    void RequestClose() noexcept { m_should_close = true; }
    void SetShouldClose(bool close) noexcept { m_should_close = close; }
    bool GetPenClick() const noexcept { return m_pen_click; }
    void TogglePenClick();
    std::string GetWindowTitle() const;
    bool NeedsContinuousRedraw() const noexcept {
        return m_config.debug_mode || m_telemetry.debug_enabled != 0 || m_drag_mode != DragMode::None || m_ui_dirty;
    }
    void MarkDirty() noexcept { m_ui_dirty = true; }
    void ClearDirty() noexcept { m_ui_dirty = false; }
    bool IsDirty() const noexcept { return m_ui_dirty; }

    bool IsManualOpen() const noexcept { return m_show_manual; }
    void SetShowManual(bool show) noexcept { m_show_manual = show; }
    bool IsDebugOpen() const noexcept { return m_config.debug_mode; }
    void SetDebugOpen(bool open);
    void RenderManualWindowContent();
    void RenderDebugWindowContent();
    void CopyDiagnosticsToClipboard();
    void SetBackgroundImage(const std::string& path);
    void OpenImageFileDialog();
    void ReloadBackgroundTexture();
    const DriverTelemetry& GetTelemetry() const noexcept { return m_telemetry; }
    const DebugSnapshot& GetDiagnostics() const noexcept { return m_debug_snapshot; }

private:
    void RenderCards(float sc);
    void RenderMainTabs(float sc);
    void RenderSettingsSubtabs(float sc);
    void RenderAreaControls(float sc);
    void RenderTuneControls(float sc);
    void RenderLookControls(float sc);
    void RenderThemeControls(float sc);
    void RenderMiscControls(float sc);
    void RenderTabletPreview(float sc);
    void RenderManualDialog(float sc);

    // Upstream math and actions
    void AutoMatchRatio();
    void AutoMatchAreaRatio();
    void ResetToFullArea();

    void SyncFromConfig();
    void SyncToConfig();
    void PushLiveConfig();

    std::string m_config_path;
    OsuPointConfig m_config{};
    ConfigManager m_config_mgr;
    ShmClient m_shm_client;
    bool m_owns_driver = false;
    bool m_ui_dirty = true;

    // Area & Transform state
    double m_width_mm = 152.0;
    double m_height_mm = 95.0;
    double m_cx_mm = 76.0;
    double m_cy_mm = 47.5;
    double m_rotation_deg = 0.0;
    double m_phys_w_mm = 152.0;
    double m_phys_h_mm = 95.0;

    bool m_lock_ratio = false;
    bool m_lock_drag = false;
    bool m_clip_area = false;
    bool m_pen_click = true;
    bool m_barrel_button = true;

    int m_active_tab = 0; // 0 = Area, 1 = Settings, 2 = Misc
    int m_settings_subtab = 0; // 0 = Tune, 1 = Look, 2 = Theme
    bool m_show_manual = false;
    bool m_should_close = false;
    bool m_dirty_config = false;

    // Buffers for text inputs
    char m_edit_w[32] = "152";
    char m_edit_h[32] = "95";
    char m_edit_cx[32] = "76";
    char m_edit_cy[32] = "47.5";
    char m_edit_rot[32] = "0";
    char m_edit_ratio[32] = "16:9";
    char m_new_preset_name[64] = "MyPreset";

    // Preview interaction state
    enum class DragMode { None, Move, ResizeCorner0, ResizeCorner1, ResizeCorner2, ResizeCorner3 };
    DragMode m_drag_mode = DragMode::None;
    ImVec2 m_drag_start_mouse{0, 0};
    double m_drag_orig_w = 0;
    double m_drag_orig_h = 0;
    double m_drag_orig_cx = 0;
    double m_drag_orig_cy = 0;

    DriverTelemetry m_telemetry{};
    DebugSnapshot m_debug_snapshot{};
    std::chrono::steady_clock::time_point m_last_telemetry_read{};
    std::chrono::steady_clock::time_point m_last_diagnostics_read{};
    std::chrono::steady_clock::time_point m_last_config_save{};

    GLFWwindow* m_glfw_window = nullptr;
    float m_drag_accum_px = 0.0f;
    bool m_was_dragged = false;
    char m_modal_num_buf[64] = "";
    std::string m_active_modal_id = "";
    unsigned int m_bg_texture = 0;
    int m_bg_tex_w = 0, m_bg_tex_h = 0;
    std::string m_loaded_bg_path = "";
    std::string m_pending_bg_path = "";
    std::mutex m_bg_mutex;

    bool DrawDraggableValueButton(const char* id, const char* label,
                                  int x, int y, int w, int h, float sc,
                                  double& val, double min_val, double max_val, double step,
                                  double toggle_val, const char* format_str,
                                  const char* zero_label = nullptr,
                                  bool is_integer = false);

    bool DrawDraggableValueButton(const char* id, const char* label,
                                  int x, int y, int w, int h, float sc,
                                  int& val, int min_val, int max_val, int step,
                                  int toggle_val, const char* format_str,
                                  const char* zero_label = nullptr) {
        double d_val = static_cast<double>(val);
        bool changed = DrawDraggableValueButton(id, label, x, y, w, h, sc,
                                               d_val, static_cast<double>(min_val),
                                               static_cast<double>(max_val),
                                               static_cast<double>(step),
                                               static_cast<double>(toggle_val),
                                               format_str, zero_label, true);
        if (changed) {
            val = static_cast<int>(std::round(d_val));
        }
        return changed;
    }
};

} // namespace osupoint::gui
