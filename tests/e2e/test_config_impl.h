#pragma once

#include <string>
#include <map>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cstdint>

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
    // 42 Global settings
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
    bool antialiasing = true;
    bool ui_outline = true;
    double extrapolation_mm = 2.5;
    int filter = 25;
    int motion_filter = 40;
    int spin_percent = 0;
    bool debug_mode = false;
    bool unlock_limits = false;
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

    void ClampBounds() {
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
        bg_blur_type = std::clamp(bg_blur_type, 0, 2);
        active_tab = std::clamp(active_tab, 0, 1);
    }
};

class ConfigParser {
public:
    static std::string Trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    static std::string StripQuotes(const std::string& s) {
        std::string res = Trim(s);
        if (res.size() >= 2 && ((res.front() == '"' && res.back() == '"') ||
                                (res.front() == '\'' && res.back() == '\''))) {
            return res.substr(1, res.size() - 2);
        }
        return res;
    }

    static double SafeStod(const std::string& s, double defVal = 0.0) {
        try {
            return std::stod(s);
        } catch (...) {
            return defVal;
        }
    }

    static int SafeStoi(const std::string& s, int defVal = 0) {
        try {
            return std::stoi(s);
        } catch (...) {
            return defVal;
        }
    }

    static bool ParseBool(const std::string& val, bool defVal = false) {
        std::string s = Trim(val);
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        if (s == "1" || s == "true" || s == "yes" || s == "on") return true;
        if (s == "0" || s == "false" || s == "no" || s == "off") return false;
        return defVal;
    }

    static OsuPointConfig ParseIni(const std::string& content) {
        OsuPointConfig cfg;
        std::istringstream stream(content);
        std::string line;
        std::string currentSection = "global";

        while (std::getline(stream, line)) {
            line = Trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;

            if (line.front() == '[' && line.back() == ']') {
                currentSection = Trim(line.substr(1, line.size() - 2));
                continue;
            }

            size_t eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;

            std::string key = Trim(line.substr(0, eqPos));
            std::string rawVal = Trim(line.substr(eqPos + 1));
            std::string val = StripQuotes(rawVal);

            if (currentSection == "global") {
                if (key == "width") cfg.width = SafeStod(val, cfg.width);
                else if (key == "height") cfg.height = SafeStod(val, cfg.height);
                else if (key == "center_x") cfg.center_x = SafeStod(val, cfg.center_x);
                else if (key == "center_y") cfg.center_y = SafeStod(val, cfg.center_y);
                else if (key == "rotation") cfg.rotation = SafeStod(val, cfg.rotation);
                else if (key == "monitor") cfg.monitor = SafeStoi(val, cfg.monitor);
                else if (key == "display_width") cfg.display_width = SafeStoi(val, cfg.display_width);
                else if (key == "display_height") cfg.display_height = SafeStoi(val, cfg.display_height);
                else if (key == "display_x") cfg.display_x = SafeStoi(val, cfg.display_x);
                else if (key == "display_y") cfg.display_y = SafeStoi(val, cfg.display_y);
                else if (key == "lock_ratio") cfg.lock_ratio = ParseBool(val, cfg.lock_ratio);
                else if (key == "ratio") cfg.ratio = val;
                else if (key == "ratio_precision") cfg.ratio_precision = SafeStoi(val, cfg.ratio_precision);
                else if (key == "lock_drag") cfg.lock_drag = ParseBool(val, cfg.lock_drag);
                else if (key == "clip_area") cfg.clip_area = ParseBool(val, cfg.clip_area);
                else if (key == "pen_click") cfg.pen_click = ParseBool(val, cfg.pen_click);
                else if (key == "barrel_button") cfg.barrel_button = ParseBool(val, cfg.barrel_button);
                else if (key == "tip_pressure_threshold") cfg.tip_pressure_threshold = SafeStoi(val, cfg.tip_pressure_threshold);
                else if (key == "antialiasing") cfg.antialiasing = ParseBool(val, cfg.antialiasing);
                else if (key == "ui_outline") cfg.ui_outline = ParseBool(val, cfg.ui_outline);
                else if (key == "extrapolation_mm") cfg.extrapolation_mm = SafeStod(val, cfg.extrapolation_mm);
                else if (key == "filter") cfg.filter = SafeStoi(val, cfg.filter);
                else if (key == "motion_filter") cfg.motion_filter = SafeStoi(val, cfg.motion_filter);
                else if (key == "spin_percent") cfg.spin_percent = SafeStoi(val, cfg.spin_percent);
                else if (key == "debug_mode") cfg.debug_mode = ParseBool(val, cfg.debug_mode);
                else if (key == "unlock_limits") cfg.unlock_limits = ParseBool(val, cfg.unlock_limits);
                else if (key == "palette") cfg.palette = val;
                else if (key == "theme_base") cfg.theme_base = val;
                else if (key == "bg_image") cfg.bg_image = val;
                else if (key == "copy_bg_image") cfg.copy_bg_image = ParseBool(val, cfg.copy_bg_image);
                else if (key == "delete_old_bg_image") cfg.delete_old_bg_image = ParseBool(val, cfg.delete_old_bg_image);
                else if (key == "copied_bg_images") cfg.copied_bg_images = val;
                else if (key == "ui_opacity") cfg.ui_opacity = SafeStoi(val, cfg.ui_opacity);
                else if (key == "bg_blur") cfg.bg_blur = SafeStoi(val, cfg.bg_blur);
                else if (key == "ui_style") cfg.ui_style = SafeStoi(val, cfg.ui_style);
                else if (key == "bg_blur_type") cfg.bg_blur_type = SafeStoi(val, cfg.bg_blur_type);
                else if (key == "ui_outline_opacity") cfg.ui_outline_opacity = SafeStoi(val, cfg.ui_outline_opacity);
                else if (key == "ui_area_opacity") cfg.ui_area_opacity = SafeStoi(val, cfg.ui_area_opacity);
                else if (key == "active_tab") cfg.active_tab = SafeStoi(val, cfg.active_tab);
                else if (key == "active_preset") cfg.active_preset = val;
            } else if (currentSection.rfind("preset:", 0) == 0) {
                std::string presetName = currentSection.substr(7);
                PresetConfig& p = cfg.presets[presetName];
                p.name = presetName;
                if (key == "width") p.width = SafeStod(val, p.width);
                else if (key == "height") p.height = SafeStod(val, p.height);
                else if (key == "center_x") p.center_x = SafeStod(val, p.center_x);
                else if (key == "center_y") p.center_y = SafeStod(val, p.center_y);
                else if (key == "rotation") p.rotation = SafeStod(val, p.rotation);
                else if (key == "ratio") p.ratio = val;
                else if (key == "monitor") p.monitor = SafeStoi(val, p.monitor);
                else if (key == "palette") p.palette = val;
            } else if (currentSection.rfind("palette:", 0) == 0) {
                std::string palName = currentSection.substr(8);
                PaletteConfig& pal = cfg.palettes[palName];
                pal.name = palName;
                if (key == "accent") pal.accent = val;
                else if (key == "bg_image") pal.bg_image = val;
            }
        }

        cfg.ClampBounds();
        return cfg;
    }

    static std::string SerializeIni(const OsuPointConfig& cfg) {
        std::ostringstream ss;
        ss << "# osu!Point Configuration\n";
        ss << "width=" << cfg.width << "\n";
        ss << "height=" << cfg.height << "\n";
        ss << "center_x=" << cfg.center_x << "\n";
        ss << "center_y=" << cfg.center_y << "\n";
        ss << "rotation=" << cfg.rotation << "\n";
        ss << "monitor=" << cfg.monitor << "\n";
        ss << "display_width=" << cfg.display_width << "\n";
        ss << "display_height=" << cfg.display_height << "\n";
        ss << "display_x=" << cfg.display_x << "\n";
        ss << "display_y=" << cfg.display_y << "\n";
        ss << "lock_ratio=" << (cfg.lock_ratio ? 1 : 0) << "\n";
        ss << "ratio=" << cfg.ratio << "\n";
        ss << "ratio_precision=" << cfg.ratio_precision << "\n";
        ss << "lock_drag=" << (cfg.lock_drag ? 1 : 0) << "\n";
        ss << "clip_area=" << (cfg.clip_area ? 1 : 0) << "\n";
        ss << "pen_click=" << (cfg.pen_click ? 1 : 0) << "\n";
        ss << "barrel_button=" << (cfg.barrel_button ? 1 : 0) << "\n";
        ss << "tip_pressure_threshold=" << cfg.tip_pressure_threshold << "\n";
        ss << "antialiasing=" << (cfg.antialiasing ? 1 : 0) << "\n";
        ss << "ui_outline=" << (cfg.ui_outline ? 1 : 0) << "\n";
        ss << "extrapolation_mm=" << cfg.extrapolation_mm << "\n";
        ss << "filter=" << cfg.filter << "\n";
        ss << "motion_filter=" << cfg.motion_filter << "\n";
        ss << "spin_percent=" << cfg.spin_percent << "\n";
        ss << "debug_mode=" << (cfg.debug_mode ? 1 : 0) << "\n";
        ss << "unlock_limits=" << (cfg.unlock_limits ? 1 : 0) << "\n";
        ss << "palette=" << cfg.palette << "\n";
        ss << "theme_base=" << cfg.theme_base << "\n";
        ss << "bg_image=" << cfg.bg_image << "\n";
        ss << "copy_bg_image=" << (cfg.copy_bg_image ? 1 : 0) << "\n";
        ss << "delete_old_bg_image=" << (cfg.delete_old_bg_image ? 1 : 0) << "\n";
        ss << "copied_bg_images=" << cfg.copied_bg_images << "\n";
        ss << "ui_opacity=" << cfg.ui_opacity << "\n";
        ss << "bg_blur=" << cfg.bg_blur << "\n";
        ss << "ui_style=" << cfg.ui_style << "\n";
        ss << "bg_blur_type=" << cfg.bg_blur_type << "\n";
        ss << "ui_outline_opacity=" << cfg.ui_outline_opacity << "\n";
        ss << "ui_area_opacity=" << cfg.ui_area_opacity << "\n";
        ss << "active_tab=" << cfg.active_tab << "\n";
        ss << "active_preset=" << cfg.active_preset << "\n";

        for (const auto& [name, preset] : cfg.presets) {
            ss << "\n[preset:" << name << "]\n";
            ss << "width=" << preset.width << "\n";
            ss << "height=" << preset.height << "\n";
            ss << "center_x=" << preset.center_x << "\n";
            ss << "center_y=" << preset.center_y << "\n";
            ss << "rotation=" << preset.rotation << "\n";
            ss << "ratio=" << preset.ratio << "\n";
            ss << "monitor=" << preset.monitor << "\n";
            ss << "palette=" << preset.palette << "\n";
        }

        for (const auto& [name, pal] : cfg.palettes) {
            ss << "\n[palette:" << name << "]\n";
            ss << "accent=" << pal.accent << "\n";
            ss << "bg_image=" << pal.bg_image << "\n";
        }

        return ss.str();
    }
};
