#include "osupoint/config_manager.h"
#include "osupoint/transform_pipeline.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/stat.h>
#include <filesystem>
#include <limits>
#include <fcntl.h>
#include <cerrno>

namespace osupoint {

void OsuPointConfig::ToTransformConfig(TransformConfig& out) const noexcept {
    ConfigManager::ToTransformConfig(*this, out);
}

void OsuPointConfig::ToTransformConfig(const RuntimeTabletSpec& spec, TransformConfig& out) const noexcept {
    ConfigManager::ToTransformConfig(*this, out);

    if (spec.max_x > 0 && spec.max_y > 0 && spec.phys_w > 0.0 && spec.phys_h > 0.0) {
        AreaConfig area;
        area.width_mm = this->width;
        area.height_mm = this->height;
        area.cx_mm = this->center_x;
        area.cy_mm = this->center_y;
        area.rotation_deg = this->rotation;
        area.monitor_idx = this->monitor;
        area.display_x = this->display_x;
        area.display_y = this->display_y;
        area.display_w = this->display_width;
        area.display_h = this->display_height;
        area.clip_area = this->clip_area;
        area.unlock_limits = this->unlock_limits;

        MonitorRect mon{0, 0, 1920, 1080};
        VirtualDesktop virt{0.0, 0.0, 1920.0, 1080.0};
        if (cached_desktop[2] > 1 && cached_desktop[3] > 1) {
            virt = {double(cached_desktop[0]), double(cached_desktop[1]), double(cached_desktop[2]), double(cached_desktop[3])};
            const int* rect = monitor < 0 || cached_monitor[2] < 2 || cached_monitor[3] < 2 ? cached_desktop : cached_monitor;
            mon.left = rect[0]; mon.top = rect[1]; mon.right = rect[0] + rect[2]; mon.bottom = rect[1] + rect[3];
        }
        ComputeTransform(spec, area, mon, virt, out);
    }
}

void OsuPointConfig::FromTransformConfig(const TransformConfig& in) noexcept {
    ConfigManager::FromTransformConfig(in, *this);
}

void OsuPointConfig::ToTransformConfig(const RuntimeTabletSpec& spec, const DisplayGeometry& geometry,
                                     TransformConfig& out) const noexcept {
    ConfigManager::ToTransformConfig(*this, out);
    AreaConfig area;
    area.width_mm = width; area.height_mm = height;
    area.cx_mm = center_x; area.cy_mm = center_y; area.rotation_deg = rotation;
    area.monitor_idx = monitor; area.display_x = display_x; area.display_y = display_y;
    area.display_w = display_width; area.display_h = display_height;
    area.clip_area = clip_area; area.unlock_limits = unlock_limits;
    MonitorRect mon;
    const auto& desktop = geometry.virtual_desktop;
    if (monitor < 0 || geometry.monitor_count == 0) {
        mon.left = static_cast<int>(desktop.x); mon.top = static_cast<int>(desktop.y);
        mon.right = mon.left + static_cast<int>(desktop.width);
        mon.bottom = mon.top + static_cast<int>(desktop.height);
    } else {
        const size_t index = static_cast<size_t>(std::clamp(monitor, 0, static_cast<int>(geometry.monitor_count) - 1));
        mon = geometry.monitors[index];
    }
    ComputeTransform(spec, area, mon, desktop, out);
}

std::string ConfigManager::Trim(const std::string& str) noexcept {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

std::string ConfigManager::StripQuotes(const std::string& s) noexcept {
    std::string res = Trim(s);
    if (res.size() >= 2 && ((res.front() == '"' && res.back() == '"') ||
                            (res.front() == '\'' && res.back() == '\''))) {
        return res.substr(1, res.size() - 2);
    }
    return res;
}

double ConfigManager::SafeStod(const std::string& s, double defVal) noexcept {
    if (s.empty()) return defVal;
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() && std::isfinite(v)) {
        return v;
    }
    return defVal;
}

int ConfigManager::SafeStoi(const std::string& s, int defVal) noexcept {
    if (s.empty()) return defVal;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (end != s.c_str() && v >= std::numeric_limits<int>::min() && v <= std::numeric_limits<int>::max()) {
        return static_cast<int>(v);
    }
    return defVal;
}

bool ConfigManager::ParseBool(const std::string& val, bool defVal) noexcept {
    std::string s = Trim(val);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (s == "1" || s == "true" || s == "yes" || s == "on") return true;
    if (s == "0" || s == "false" || s == "no" || s == "off") return false;
    return defVal;
}

OsuPointConfig ConfigManager::ParseIni(const std::string& content) {
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
            else if (key == "pressure_max") cfg.pressure_max = SafeStoi(val, cfg.pressure_max);
            else if (key == "desktop_left") cfg.cached_desktop[0] = SafeStoi(val);
            else if (key == "desktop_top") cfg.cached_desktop[1] = SafeStoi(val);
            else if (key == "desktop_width") cfg.cached_desktop[2] = SafeStoi(val);
            else if (key == "desktop_height") cfg.cached_desktop[3] = SafeStoi(val);
            else if (key == "mapped_left") cfg.cached_monitor[0] = SafeStoi(val);
            else if (key == "mapped_top") cfg.cached_monitor[1] = SafeStoi(val);
            else if (key == "mapped_width") cfg.cached_monitor[2] = SafeStoi(val);
            else if (key == "mapped_height") cfg.cached_monitor[3] = SafeStoi(val);
            else if (key == "antialiasing") cfg.antialiasing = ParseBool(val, cfg.antialiasing);
            else if (key == "ui_outline") cfg.ui_outline = ParseBool(val, cfg.ui_outline);
            else if (key == "extrapolation_mm") cfg.extrapolation_mm = SafeStod(val, cfg.extrapolation_mm);
            else if (key == "filter") cfg.filter = SafeStoi(val, cfg.filter);
            else if (key == "motion_filter") cfg.motion_filter = SafeStoi(val, cfg.motion_filter);
            else if (key == "spin_percent") cfg.spin_percent = SafeStoi(val, cfg.spin_percent);
            else if (key == "debug_mode") cfg.debug_mode = ParseBool(val, cfg.debug_mode);
            else if (key == "mouse_mode") cfg.mouse_mode = ParseBool(val, cfg.mouse_mode);
            else if (key == "unlock_limits") cfg.unlock_limits = ParseBool(val, cfg.unlock_limits);
            else if (key == "user_cutoff" || key == "UserCutoff") cfg.user_cutoff = SafeStod(val, cfg.user_cutoff);
            else if (key == "user_v_start" || key == "UserVStart") cfg.user_v_start = SafeStod(val, cfg.user_v_start);
            else if (key == "user_v_full" || key == "UserVFull") cfg.user_v_full = SafeStod(val, cfg.user_v_full);
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
        } else if (currentSection == "Filter" || currentSection == "filter") {
            if (key == "UserCutoff" || key == "user_cutoff") cfg.user_cutoff = SafeStod(val, cfg.user_cutoff);
            else if (key == "UserVStart" || key == "user_v_start") cfg.user_v_start = SafeStod(val, cfg.user_v_start);
            else if (key == "UserVFull" || key == "user_v_full") cfg.user_v_full = SafeStod(val, cfg.user_v_full);
        }
    }

    cfg.ClampBounds();
    return cfg;
}

std::string ConfigManager::SerializeIni(const OsuPointConfig& cfg) {
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
    ss << "pressure_max=" << cfg.pressure_max << "\n";
    const char* desktop_keys[] = {"desktop_left", "desktop_top", "desktop_width", "desktop_height"};
    const char* monitor_keys[] = {"mapped_left", "mapped_top", "mapped_width", "mapped_height"};
    for (int i = 0; i < 4; ++i) {
        ss << desktop_keys[i] << '=' << cfg.cached_desktop[i] << '\n';
        ss << monitor_keys[i] << '=' << cfg.cached_monitor[i] << '\n';
    }
    ss << "antialiasing=" << (cfg.antialiasing ? 1 : 0) << "\n";
    ss << "ui_outline=" << (cfg.ui_outline ? 1 : 0) << "\n";
    ss << "extrapolation_mm=" << cfg.extrapolation_mm << "\n";
    ss << "filter=" << cfg.filter << "\n";
    ss << "motion_filter=" << cfg.motion_filter << "\n";
    ss << "spin_percent=" << cfg.spin_percent << "\n";
    ss << "debug_mode=" << (cfg.debug_mode ? 1 : 0) << "\n";
    ss << "mouse_mode=" << (cfg.mouse_mode ? 1 : 0) << "\n";
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

    if (cfg.user_cutoff > 0.0 || cfg.user_v_start > 0.0 || cfg.user_v_full > 0.0) {
        ss << "\n[Filter]\n";
        if (cfg.user_cutoff > 0.0) ss << "UserCutoff=" << cfg.user_cutoff << "\n";
        if (cfg.user_v_start > 0.0) ss << "UserVStart=" << cfg.user_v_start << "\n";
        if (cfg.user_v_full > 0.0) ss << "UserVFull=" << cfg.user_v_full << "\n";
    }

    return ss.str();
}

bool ConfigManager::LoadFromFile(const std::string& path, OsuPointConfig& outCfg) {
    std::string targetPath = path.empty() ? GetDefaultConfigPath() : path;
    std::ifstream file(targetPath);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    outCfg = ParseIni(buffer.str());
    return true;
}

bool ConfigManager::SaveToFile(const std::string& path, const OsuPointConfig& cfg) {
    std::string targetPath = path.empty() ? GetDefaultConfigPath() : path;

    // Ensure parent directory exists if path contains '/'
    size_t lastSlash = targetPath.rfind('/');
    if (lastSlash != std::string::npos) {
        std::string dir = targetPath.substr(0, lastSlash);
        std::error_code error;
        std::filesystem::create_directories(dir, error);
        if (error) return false;
    }

    std::string tmpPath = targetPath + ".tmp." + std::to_string(getpid());
    std::ofstream file(tmpPath);
    if (!file.is_open()) {
        return false;
    }

    std::string content = SerializeIni(cfg);
    file << content;
    file.flush();
    file.close();

    if (std::rename(tmpPath.c_str(), targetPath.c_str()) != 0) {
        unlink(tmpPath.c_str());
        return false;
    }

    return true;
}

void ConfigManager::ToTransformConfig(const OsuPointConfig& cfg, TransformConfig& tc) noexcept {
    tc.mouse_mode = cfg.mouse_mode ? 1 : 0;
    tc.width_mm = static_cast<float>(cfg.width);
    tc.height_mm = static_cast<float>(cfg.height);
    tc.cx = static_cast<float>(cfg.center_x);
    tc.cy = static_cast<float>(cfg.center_y);
    tc.rotation_deg = static_cast<float>(cfg.rotation);
    tc.monitor_idx = cfg.monitor;
    tc.extrapolation_tau = static_cast<float>(cfg.extrapolation_mm);
    tc.extrapolation = (cfg.extrapolation_mm > 0.0) ? 1 : 0;
    tc.filter_1euro = static_cast<uint8_t>(std::clamp(cfg.filter, 0, 100));
    tc.filter_motion = static_cast<uint8_t>(std::clamp(cfg.motion_filter, 0, 100));
    tc.spin_percent = cfg.spin_percent;
    tc.clip_area = cfg.clip_area ? 1 : 0;
    tc.pen_click = cfg.pen_click ? 1 : 0;
    tc.barrel_btn = cfg.barrel_button ? 1 : 0;
    tc.debug_mode = cfg.debug_mode ? 1 : 0;
    tc.tip_pressure_threshold = static_cast<uint16_t>(std::clamp(cfg.tip_pressure_threshold, 0, 65535));
    tc.pressure_max = static_cast<uint16_t>(std::clamp(cfg.pressure_max, 1, 65535));
    tc.user_cutoff = static_cast<float>(cfg.user_cutoff);
    tc.user_v_start = static_cast<float>(cfg.user_v_start);
    tc.user_v_full = static_cast<float>(cfg.user_v_full);
    tc.unlock_limits = cfg.unlock_limits ? 1 : 0;
}

void ConfigManager::FromTransformConfig(const TransformConfig& tc, OsuPointConfig& cfg) noexcept {
    cfg.width = tc.width_mm;
    cfg.height = tc.height_mm;
    cfg.center_x = tc.cx;
    cfg.center_y = tc.cy;
    cfg.rotation = tc.rotation_deg;
    cfg.monitor = tc.monitor_idx;
    cfg.extrapolation_mm = (tc.extrapolation != 0) ? tc.extrapolation_tau : 0.0;
    cfg.filter = tc.filter_1euro;
    cfg.motion_filter = tc.filter_motion;
    cfg.spin_percent = tc.spin_percent;
    cfg.clip_area = (tc.clip_area != 0);
    cfg.pen_click = (tc.pen_click != 0);
    cfg.barrel_button = (tc.barrel_btn != 0);
    cfg.mouse_mode = (tc.mouse_mode != 0);
    cfg.debug_mode = (tc.debug_mode != 0);
    cfg.tip_pressure_threshold = tc.tip_pressure_threshold;
    cfg.pressure_max = tc.pressure_max;
    cfg.user_cutoff = tc.user_cutoff;
    cfg.user_v_start = tc.user_v_start;
    cfg.user_v_full = tc.user_v_full;
    cfg.unlock_limits = (tc.unlock_limits != 0);
}

std::string ConfigManager::GetDefaultConfigPath() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] != '\0') {
        return std::string(xdg) + "/osupoint/config.ini";
    }
    const char* home = std::getenv("HOME");
    if (home && home[0] != '\0') {
        return std::string(home) + "/.config/osupoint/config.ini";
    }
    return "./config.ini";
}

} // namespace osupoint
