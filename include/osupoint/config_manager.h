#pragma once

#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include "osupoint/ipc_protocol.h"
#include "osupoint/tablet_spec.h"

namespace osupoint {

struct DisplayGeometry;

using TabletSpec = RuntimeTabletSpec;

struct PresetConfig {
    std::string name = "Default";
    double width = 152.0;
    double height = 95.0;
    double center_x = 76.0;
    double center_y = 47.5;
    double rotation = 0.0;
    std::string ratio = "16:9";
    int monitor = 0;
    std::string palette = "system";
};

struct PaletteConfig {
    std::string name = "Custom";
    std::string accent = "#FFFFFF";
    std::string bg_image = "";
};

struct OsuPointConfig {
    // 40 Global settings
    double width = 152.0;
    double height = 95.0;
    double center_x = 76.0;
    double center_y = 47.5;
    double rotation = 0.0;
    int monitor = 0;
    int display_width = 0;
    int display_height = 0;
    int display_x = -1;
    int display_y = -1;
    bool lock_ratio = false;
    std::string ratio = "16:9";
    int ratio_precision = 8;
    bool lock_drag = false;
    bool clip_area = false;
    bool pen_click = true;
    bool barrel_button = true;
    int tip_pressure_threshold = 0;
    int pressure_max = 8191;
    int cached_desktop[4] = {}; // left, top, width, height; last GUI mapping
    int cached_monitor[4] = {};
    bool antialiasing = true;
    bool ui_outline = true;
    double extrapolation_mm = 0.0;
    int filter = 0;
    int motion_filter = 0;
    int spin_percent = 0;
    bool debug_mode = false;
    bool mouse_mode = true;
    bool unlock_limits = false;
    double user_cutoff = 0.0;
    double user_v_start = 0.0;
    double user_v_full = 0.0;
    std::string palette = "amber";
    std::string theme_base = "dark";
    std::string bg_image = "";
    bool copy_bg_image = true;
    bool delete_old_bg_image = false;
    std::string copied_bg_images = "";
    int ui_opacity = 100;
    int bg_blur = 18;
    int ui_style = 0;
    int bg_blur_type = 1;
    int ui_outline_opacity = 0;
    int ui_area_opacity = 0;
    int active_tab = 0;
    std::string active_preset = "Default";

    // Presets and Palettes
    std::map<std::string, PresetConfig> presets;
    std::map<std::string, PaletteConfig> palettes;

    void ClampBounds() noexcept {
        if (!std::isfinite(width)) width = 152;
        if (!std::isfinite(height)) height = 95;
        if (!std::isfinite(center_x)) center_x = 76;
        if (!std::isfinite(center_y)) center_y = 47.5;
        if (!std::isfinite(rotation)) rotation = 0;
        if (!std::isfinite(extrapolation_mm)) extrapolation_mm = 0;
        if (!std::isfinite(user_cutoff)) user_cutoff = 0;
        if (!std::isfinite(user_v_start)) user_v_start = 0;
        if (!std::isfinite(user_v_full)) user_v_full = 0;
        if (unlock_limits) {
            width = (std::max)(width, 0.1);
            height = (std::max)(height, 0.1);
            rotation = std::remainder(rotation, 360.0);
            tip_pressure_threshold = std::clamp(tip_pressure_threshold, 0, 65535);
            pressure_max = std::clamp(pressure_max, 1, 65535);
            filter = (std::max)(filter, 0);
            motion_filter = (std::max)(motion_filter, 0);
            extrapolation_mm = (std::max)(extrapolation_mm, 0.0);
            spin_percent = std::clamp(spin_percent, 0, 100);
            ratio_precision = std::clamp(ratio_precision, 1, 8);
            ui_opacity = std::clamp(ui_opacity, 10, 100);
            ui_outline_opacity = std::clamp(ui_outline_opacity, 0, 100);
            ui_area_opacity = std::clamp(ui_area_opacity, 0, 100);
            ui_style = std::clamp(ui_style, 0, 2);
            bg_blur = std::clamp(bg_blur, 0, 20);
            bg_blur_type = std::clamp(bg_blur_type, 0, 3);
            active_tab = std::clamp(active_tab, 0, 2);
            return;
        }
        width = std::clamp(width, 1.0, 100000.0);
        height = std::clamp(height, 1.0, 100000.0);
        rotation = std::remainder(rotation, 360.0);
        tip_pressure_threshold = std::clamp(tip_pressure_threshold, 0, 65535);
        pressure_max = std::clamp(pressure_max, 1, 65535);
        for (int i = 0; i < 4; ++i) {
            cached_desktop[i] = std::clamp(cached_desktop[i], i < 2 ? -100000 : 0, 100000);
            cached_monitor[i] = std::clamp(cached_monitor[i], i < 2 ? -100000 : 0, 100000);
        }
        ui_opacity = std::clamp(ui_opacity, 10, 100);
        bg_blur = std::clamp(bg_blur, 0, 20);
        filter = std::clamp(filter, 0, 100);
        motion_filter = std::clamp(motion_filter, 0, 100);
        spin_percent = std::clamp(spin_percent, 0, 100);
        extrapolation_mm = std::clamp(extrapolation_mm, 0.0, 5.0);
        ratio_precision = std::clamp(ratio_precision, 1, 8);
        ui_outline_opacity = std::clamp(ui_outline_opacity, 0, 100);
        ui_area_opacity = std::clamp(ui_area_opacity, 0, 100);
        ui_style = std::clamp(ui_style, 0, 2);
        bg_blur_type = std::clamp(bg_blur_type, 0, 3);
        active_tab = std::clamp(active_tab, 0, 2);
    }

    void ToTransformConfig(TransformConfig& out) const noexcept;
    void ToTransformConfig(const RuntimeTabletSpec& spec, TransformConfig& out) const noexcept;
    void ToTransformConfig(const RuntimeTabletSpec& spec, const DisplayGeometry& geometry, TransformConfig& out) const noexcept;
    void FromTransformConfig(const TransformConfig& in) noexcept;
};

class ConfigManager {
public:
    static std::string Trim(const std::string& str) noexcept;
    static std::string StripQuotes(const std::string& s) noexcept;
    static double SafeStod(const std::string& s, double defVal = 0.0) noexcept;
    static int SafeStoi(const std::string& s, int defVal = 0) noexcept;
    static bool ParseBool(const std::string& val, bool defVal = false) noexcept;

    static OsuPointConfig ParseIni(const std::string& content);
    static std::string SerializeIni(const OsuPointConfig& cfg);

    static bool LoadFromFile(const std::string& path, OsuPointConfig& outCfg);
    static bool SaveToFile(const std::string& path, const OsuPointConfig& cfg);

    static void ToTransformConfig(const OsuPointConfig& cfg, TransformConfig& tc) noexcept;
    static void FromTransformConfig(const TransformConfig& tc, OsuPointConfig& cfg) noexcept;

    static std::string GetDefaultConfigPath();
};

using ConfigParser = ConfigManager;

} // namespace osupoint

// Expose in global scope for test runner compatibility
using osupoint::PresetConfig;
using osupoint::PaletteConfig;
using osupoint::OsuPointConfig;
using osupoint::ConfigManager;
using osupoint::ConfigParser;
