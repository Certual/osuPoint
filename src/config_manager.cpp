#pragma code_seg(".text$cold")
#include "config_manager.h"
#include "driver_pipeline.h"
#include "platform.h"
#include "ui_view.h"

std::vector<DisplayMonitor> g_monitors;
int g_target_monitor_idx = 0;
int g_disp_w = 0;
int g_disp_h = 0;
int g_disp_x = -1;
int g_disp_y = -1;

FILETIME g_lastConfigTime = { 0 };
std::string g_baseDir = "";

bool g_lockRatio = false;
std::string g_configRatio = "16:9";
int g_ratioPrecision = 2;
bool g_lockDrag = false;

std::vector<AreaPreset> g_presets;
std::string g_activePresetName = "Default";
bool g_antialiasing = true;
bool g_extrapolation = false;
double g_extrapolationTau = 2.5;
int g_spinPercent = 0;
std::string g_activePalette = "system";
std::string g_bgImagePath = "";
bool g_copyBgImage = true;
bool g_deleteOldBgImage = false;
std::vector<std::string> g_copiedBgImages;
int g_uiOpacity = 80;
int g_bgBlur = 0;
int g_activeTab = 0;
std::vector<CustomPalette> g_customPalettes;

std::vector<TabletSpec> g_cachedSpecs;
bool g_specsCached = false;

std::string Trim(const std::string& str) noexcept {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

std::string ToLower(std::string str) noexcept {
    for (char& c : str) {
        if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
    }
    return str;
}

std::string Utf16ToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::wstring Utf8ToUtf16(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

std::string GetExeDirectory() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) *(lastSlash + 1) = L'\0';
    return Utf16ToUtf8(std::wstring(exePath));
}

USHORT ParseHexOrDec(const std::string& str) noexcept {
    if (str.rfind("0x", 0) == 0 || str.rfind("0X", 0) == 0) {
        return static_cast<USHORT>(std::strtoul(str.c_str(), nullptr, 16));
    }
    return static_cast<USHORT>(std::atoi(str.c_str()));
}

double ParseAspectRatio(const std::string& str) {
    size_t sep = str.find_first_of(":/xX, ");
    if (sep != std::string::npos) {
        double num = std::atof(str.substr(0, sep).c_str());
        double den = std::atof(str.substr(sep + 1).c_str());
        if (den > 0.0001 && num > 0.0001) return num / den;
    }
    double val = std::atof(str.c_str());
    if (val > 0.01) return val;
    return 16.0 / 9.0;
}

double ParseInput(HWND hEdit) {
    char buf[32];
    GetWindowTextA(hEdit, buf, 32);
    for (int i = 0; buf[i]; ++i) {
        if (buf[i] == ',') buf[i] = '.';
    }
    return std::atof(buf);
}

std::string FormatDouble(double v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    return s;
}

std::string FormatRatio(double r, int precision) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", std::clamp(precision, 1, 8), r);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    return s;
}

void ParseHexBytes(const char* hexStr, std::vector<uint8_t>& outBytes) {
    outBytes.clear();
    if (!hexStr) return;
    const char* p = hexStr;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') ++p;
        if (!*p) break;
        char* endP = nullptr;
        unsigned long b = std::strtoul(p, &endP, 0);
        if (p == endP) break;
        outBytes.push_back(static_cast<uint8_t>(b));
        p = endP;
    }
}

void ApplyBuiltinSpec(const BuiltinTabletDef& def, TabletSpec& spec) {
    spec.name = def.name;
    spec.vid = def.vid;
    spec.pid = def.pid;
    spec.max_x = def.max_x;
    spec.max_y = def.max_y;
    spec.phys_w = def.phys_w;
    spec.phys_h = def.phys_h;
    spec.report_len = def.report_len;
    spec.report_id = def.report_id;
    spec.x_offset = def.x_offset;
    spec.y_offset = def.y_offset;
    spec.coord_bits = (def.max_x > 0x1FFFF || def.max_y > 0x1FFFF) ? 18 :
                      ((def.max_x > 0xFFFF || def.max_y > 0xFFFF) ? 17 : 16);
    if (VendorID::IsWacom(def.vid) && (def.report_id == 0x10 || def.y_offset == 5 || (def.max_x > 0xFFFF && def.report_len >= 16))) {
        spec.coord_bits = 24;
        if (def.y_offset == 4) {
            spec.y_offset = 5;
        }
    }
    spec.init_steps.clear();
    std::vector<uint8_t> tmp;
    ParseHexBytes(def.init_feature, tmp);
    if (!tmp.empty()) spec.init_steps.push_back({InitStepType::SetFeature, tmp});
    tmp.clear();
    ParseHexBytes(def.init_output, tmp);
    if (!tmp.empty()) spec.init_steps.push_back({InitStepType::SetOutputReport, tmp});
}

static BOOL CALLBACK MonitorEnumProc(HMONITOR hMon, HDC, LPRECT, LPARAM) {
    MONITORINFOEXA mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoA(hMon, &mi)) {
        DisplayMonitor dm;
        dm.index = static_cast<int>(g_monitors.size());
        dm.rc = mi.rcMonitor;
        dm.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        dm.name = mi.szDevice;
        g_monitors.push_back(dm);
    }
    return TRUE;
}

void RefreshMonitors() {
    g_monitors.clear();
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, 0);
}

RECT GetTargetMonitorRect(int selectedIndex) {
    if (selectedIndex == -1) {
        RECT rc;
        rc.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        rc.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        rc.right = rc.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        rc.bottom = rc.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        return rc;
    }
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(g_monitors.size())) {
        return g_monitors[selectedIndex].rc;
    }
    for (const auto& m : g_monitors) {
        if (m.isPrimary) return m.rc;
    }
    return RECT{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };
}

bool LoadTabletProfile(const std::wstring& filepath, TabletSpec& spec) {
    std::ifstream f(filepath);
    if (!f.is_open()) return false;

    std::string line;
    spec.init_steps.clear();

    while (std::getline(f, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = ToLower(Trim(line.substr(0, eq)));
        std::string val = Trim(line.substr(eq + 1));

        if (key == "name") spec.name = val;
        else if (key == "vid") spec.vid = ParseHexOrDec(val);
        else if (key == "pid") spec.pid = ParseHexOrDec(val);
        else if (key == "max_x") spec.max_x = std::atoi(val.c_str());
        else if (key == "max_y") spec.max_y = std::atoi(val.c_str());
        else if (key == "width_mm") spec.phys_w = std::atof(val.c_str());
        else if (key == "height_mm") spec.phys_h = std::atof(val.c_str());
        else if (key == "report_len") spec.report_len = std::atoi(val.c_str());
        else if (key == "report_id") spec.report_id = std::atoi(val.c_str());
        else if (key == "x_offset") spec.x_offset = std::atoi(val.c_str());
        else if (key == "y_offset") spec.y_offset = std::atoi(val.c_str());
        else if (key == "mouse_mode") spec.is_mouse_mode = (std::atoi(val.c_str()) != 0);
        else if (key == "coord_bits") spec.coord_bits = std::atoi(val.c_str());
        else if (key == "tip_pressure_threshold") spec.tip_pressure_threshold = static_cast<uint16_t>(std::atoi(val.c_str()));
        else if (key == "init_feature") {
            std::vector<uint8_t> data;
            ParseHexBytes(val.c_str(), data);
            if (!data.empty()) spec.init_steps.push_back({ InitStepType::SetFeature, data });
        }
        else if (key == "init_output") {
            std::vector<uint8_t> data;
            ParseHexBytes(val.c_str(), data);
            if (!data.empty()) spec.init_steps.push_back({ InitStepType::SetOutputReport, data });
        }
        else if (key == "init_write") {
            std::vector<uint8_t> data;
            ParseHexBytes(val.c_str(), data);
            if (!data.empty()) spec.init_steps.push_back({ InitStepType::WriteFile, data });
        }
    }
    return true;
}

void InitAllTabletSpecs() {
    if (g_specsCached) return;
    g_cachedSpecs.clear();

    std::wstring searchPattern = Utf8ToUtf16(g_baseDir + "tablets\\*.cfg");
    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &ffd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                std::wstring fullPath = Utf8ToUtf16(g_baseDir + "tablets\\") + ffd.cFileName;
                TabletSpec s;
                if (LoadTabletProfile(fullPath, s)) {
                    g_cachedSpecs.push_back(s);
                }
            }
        } while (FindNextFileW(hFind, &ffd));
        FindClose(hFind);
    }

    const size_t customSpecsCount = g_cachedSpecs.size();
    g_cachedSpecs.reserve(customSpecsCount + s_builtinTabletsCount);
    for (const auto& def : s_builtinTablets) {
        bool overriddenByCustom = false;
        for (size_t i = 0; i < customSpecsCount; ++i) {
            if (g_cachedSpecs[i].vid == def.vid && g_cachedSpecs[i].pid == def.pid) {
                overriddenByCustom = true;
                break;
            }
        }
        if (!overriddenByCustom) {
            TabletSpec s;
            ApplyBuiltinSpec(def, s);
            g_cachedSpecs.push_back(std::move(s));
        }
    }
    g_specsCached = true;
}

void SetDefaults() noexcept {
    SetTransform(g_spec.phys_w, g_spec.phys_h, g_spec.phys_w * 0.5, g_spec.phys_h * 0.5, 0.0, 0);
}

void SaveConfig() {
    if (!g_guiReady) return;
    TransformConfig cfg = g_cfgStore.Load();
    std::string cfgPath = g_baseDir + "config.ini";
    std::wstring wCfgPath = Utf8ToUtf16(cfgPath);
    std::ofstream f(wCfgPath);
    if (!f.is_open()) return;

    char rBuf[32];
    if (hRatio) {
        GetWindowTextA(hRatio, rBuf, 32);
        g_configRatio = rBuf;
    } else {
        strncpy_s(rBuf, sizeof(rBuf), g_configRatio.c_str(), _TRUNCATE);
    }

    f << "# osu!Point Configuration\n";
    f << "width=" << cfg.width_mm << "\n";
    f << "height=" << cfg.height_mm << "\n";
    f << "center_x=" << cfg.cx << "\n";
    f << "center_y=" << cfg.cy << "\n";
    f << "rotation=" << cfg.rotation_deg << "\n";
    f << "monitor=" << g_target_monitor_idx << "\n";
    f << "display_width=" << g_disp_w << "\n";
    f << "display_height=" << g_disp_h << "\n";
    f << "display_x=" << g_disp_x << "\n";
    f << "display_y=" << g_disp_y << "\n";
    f << "lock_ratio=" << (g_lockRatio ? 1 : 0) << "\n";
    f << "ratio=" << rBuf << "\n";
    f << "ratio_precision=" << g_ratioPrecision << "\n";
    f << "lock_drag=" << (g_lockDrag ? 1 : 0) << "\n";
    f << "clip_area=" << (g_clipArea ? 1 : 0) << "\n";
    f << "pen_click=" << (g_penClick.load(std::memory_order_relaxed) ? 1 : 0) << "\n";
    f << "barrel_button=" << (g_barrelClick.load(std::memory_order_relaxed) ? 1 : 0) << "\n";
    f << "antialiasing=" << (g_antialiasing ? 1 : 0) << "\n";
    f << "extrapolation=" << (g_extrapolation ? 1 : 0) << "\n";
    f << "extrapolation_tau=" << FormatDouble(g_extrapolationTau) << "\n";
    f << "spin_percent=" << g_spinPercent << "\n";
    f << "debug_mode=" << (g_debugMode ? 1 : 0) << "\n";
    f << "palette=" << g_activePalette << "\n";
    f << "bg_image=" << g_bgImagePath << "\n";
    f << "copy_bg_image=" << (g_copyBgImage ? 1 : 0) << "\n";
    f << "delete_old_bg_image=" << (g_deleteOldBgImage ? 1 : 0) << "\n";
    if (!g_copiedBgImages.empty()) {
        f << "copied_bg_images=";
        for (size_t i = 0; i < g_copiedBgImages.size(); ++i) {
            if (i > 0) f << ";";
            f << g_copiedBgImages[i];
        }
        f << "\n";
    }
    f << "ui_opacity=" << g_uiOpacity << "\n";
    f << "bg_blur=" << g_bgBlur << "\n";
    f << "active_tab=" << g_activeTab << "\n";
    f << "active_preset=" << g_activePresetName << "\n";
    if (g_spec.tip_pressure_threshold > 0) {
        f << "tip_pressure_threshold=" << g_spec.tip_pressure_threshold << "\n";
    }

    for (const auto& p : g_presets) {
        f << "\n[preset:" << p.name << "]\n";
        f << "width=" << p.width_mm << "\n";
        f << "height=" << p.height_mm << "\n";
        f << "center_x=" << p.cx << "\n";
        f << "center_y=" << p.cy << "\n";
        f << "rotation=" << p.rotation_deg << "\n";
        f << "ratio=" << p.ratio << "\n";
        f << "monitor=" << p.monitor << "\n";
        if (!p.palette.empty()) f << "palette=" << p.palette << "\n";
        if (!p.bg_image.empty()) f << "bg_image=" << p.bg_image << "\n";
    }

    for (const auto& cp : g_customPalettes) {
        char hexCol[16];
        snprintf(hexCol, sizeof(hexCol), "#%02X%02X%02X",
            GetRValue(cp.color), GetGValue(cp.color), GetBValue(cp.color));
        f << "\n[palette:" << cp.name << "]\n";
        f << "accent=" << hexCol << "\n";
        if (!cp.bg_image.empty()) f << "bg_image=" << cp.bg_image << "\n";
    }

    f.close();

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(wCfgPath.c_str(), GetFileExInfoStandard, &fad)) {
        g_lastConfigTime = fad.ftLastWriteTime;
    }
}

static AreaPreset CreateDefaultPreset() {
    TransformConfig cfg = g_cfgStore.Load();
    AreaPreset def;
    def.name = "Default";
    def.width_mm = (cfg.width_mm > 0.0) ? cfg.width_mm : ((g_spec.phys_w > 0.0) ? g_spec.phys_w : 152.0);
    def.height_mm = (cfg.height_mm > 0.0) ? cfg.height_mm : ((g_spec.phys_h > 0.0) ? g_spec.phys_h : 95.0);
    def.cx = (cfg.cx > 0.0) ? cfg.cx : (def.width_mm * 0.5);
    def.cy = (cfg.cy > 0.0) ? cfg.cy : (def.height_mm * 0.5);
    def.rotation_deg = cfg.rotation_deg;
    def.ratio = g_configRatio.empty() ? "16:9" : g_configRatio;
    def.monitor = g_target_monitor_idx;
    def.palette = g_activePalette;
    def.bg_image = g_bgImagePath;
    return def;
}

void LoadConfig() {
    std::string cfgPath = g_baseDir + "config.ini";
    std::wstring wCfgPath = Utf8ToUtf16(cfgPath);
    std::ifstream f(wCfgPath);
    if (!f.is_open()) {
        SetDefaults();
        if (g_presets.empty()) {
            g_presets.push_back(CreateDefaultPreset());
        }
        return;
    }

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(wCfgPath.c_str(), GetFileExInfoStandard, &fad)) {
        g_lastConfigTime = fad.ftLastWriteTime;
    }

    std::string line;
    double w = 0, h = 0, cx = 0, cy = 0, rot = 0;
    int mon = 0;
    bool has_w = false, has_h = false, has_cx = false, has_cy = false;
    std::string ratioStr = "16:9";

    g_presets.clear();
    g_customPalettes.clear();
    std::string currentSection = "global";
    AreaPreset currentPreset{};
    CustomPalette currentPalette{};

    auto flushSection = [&]() {
        if (currentSection == "preset" && !currentPreset.name.empty()) {
            auto it = std::ranges::find_if(g_presets, [&](const auto& p) {
                return _stricmp(p.name.c_str(), currentPreset.name.c_str()) == 0;
            });
            if (it != g_presets.end()) *it = currentPreset;
            else g_presets.push_back(currentPreset);
        }
        else if (currentSection == "palette" && !currentPalette.name.empty()) {
            auto it = std::ranges::find_if(g_customPalettes, [&](const auto& cp) {
                return _stricmp(cp.name.c_str(), currentPalette.name.c_str()) == 0;
            });
            if (it != g_customPalettes.end()) *it = currentPalette;
            else g_customPalettes.push_back(currentPalette);
        }
    };

    while (std::getline(f, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || (line.size() >= 2 && line[0] == '/' && line[1] == '/')) continue;

        if (line.front() == '[' && line.back() == ']') {
            flushSection();
            std::string sec = Trim(line.substr(1, line.length() - 2));
            if (sec.rfind("preset:", 0) == 0) {
                currentSection = "preset";
                currentPreset = AreaPreset{};
                currentPreset.name = Trim(sec.substr(7));
                currentPreset.ratio = "16:9";
            } else if (sec.rfind("palette:", 0) == 0) {
                currentSection = "palette";
                currentPalette = CustomPalette{};
                currentPalette.name = Trim(sec.substr(8));
            } else {
                currentSection = "global";
            }
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = ToLower(Trim(line.substr(0, eq)));
        std::string valStr = Trim(line.substr(eq + 1));
        double val = std::atof(valStr.c_str());

        if (currentSection == "preset") {
            if (key == "width") currentPreset.width_mm = val;
            else if (key == "height") currentPreset.height_mm = val;
            else if (key == "center_x") currentPreset.cx = val;
            else if (key == "center_y") currentPreset.cy = val;
            else if (key == "rotation") currentPreset.rotation_deg = val;
            else if (key == "ratio") currentPreset.ratio = valStr;
            else if (key == "monitor") currentPreset.monitor = std::atoi(valStr.c_str());
            else if (key == "palette" || key == "color") currentPreset.palette = valStr;
            else if (key == "bg_image" || key == "background" || key == "bg" || key == "wallpaper") currentPreset.bg_image = valStr;
        }
        else if (currentSection == "palette") {
            if (key == "accent" || key == "color") {
                COLORREF col = 0;
                const char* p = valStr.c_str();
                if (*p == '#') p++;
                else if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
                unsigned int r = 0, g = 0, b = 0;
                if (sscanf_s(p, "%02x%02x%02x", &r, &g, &b) == 3) {
                    col = RGB(r, g, b);
                }
                currentPalette.color = col;
            }
            else if (key == "bg_image" || key == "background" || key == "bg" || key == "wallpaper") {
                currentPalette.bg_image = valStr;
            }
        }
        else {
            if (key == "width") { w = val; has_w = true; }
            else if (key == "height") { h = val; has_h = true; }
            else if (key == "center_x") { cx = val; has_cx = true; }
            else if (key == "center_y") { cy = val; has_cy = true; }
            else if (key == "rotation") { rot = val; }
            else if (key == "monitor") { mon = std::atoi(valStr.c_str()); }
            else if (key == "display_width" || key == "screen_width" || key == "disp_w") { g_disp_w = std::atoi(valStr.c_str()); }
            else if (key == "display_height" || key == "screen_height" || key == "disp_h") { g_disp_h = std::atoi(valStr.c_str()); }
            else if (key == "display_x" || key == "screen_x" || key == "display_offset_x" || key == "disp_x") { g_disp_x = std::atoi(valStr.c_str()); }
            else if (key == "display_y" || key == "screen_y" || key == "display_offset_y" || key == "disp_y") { g_disp_y = std::atoi(valStr.c_str()); }
            else if (key == "lock_ratio") { g_lockRatio = (std::atoi(valStr.c_str()) != 0); }
            else if (key == "ratio") { ratioStr = valStr; }
            else if (key == "ratio_precision") { g_ratioPrecision = std::clamp(std::atoi(valStr.c_str()), 1, 8); }
            else if (key == "lock_drag" || key == "lock_area") { g_lockDrag = (std::atoi(valStr.c_str()) != 0); }
            else if (key == "clip_area" || key == "clip" || key == "lock_out_of_area" || key == "ignore_outside") { g_clipArea = (std::atoi(valStr.c_str()) != 0); }
            else if (key == "pen_click" || key == "click" || key == "pen_button") { g_penClick.store(std::atoi(valStr.c_str()) != 0, std::memory_order_relaxed); }
            else if (key == "barrel_button" || key == "barrel_click" || key == "pen_right_click" || key == "right_click") { g_barrelClick.store(std::atoi(valStr.c_str()) != 0, std::memory_order_relaxed); }
            else if (key == "tip_pressure_threshold" || key == "pressure_threshold" || key == "pressure_min") { g_spec.tip_pressure_threshold = static_cast<uint16_t>(std::atoi(valStr.c_str())); }
            else if (key == "antialiasing" || key == "aa") { g_antialiasing = (std::atoi(valStr.c_str()) != 0); }
            else if (key == "extrapolation" || key == "extrapolate" || key == "kinematic_extrapolation") { g_extrapolation = (std::atoi(valStr.c_str()) != 0); }
            else if (key == "extrapolation_tau" || key == "extrapolate_tau" || key == "tau") {
                double t = std::atof(valStr.c_str());
                if (t >= 0.5 && t <= 10.0) g_extrapolationTau = t;
            }
            else if (key == "spin_percent" || key == "cpu_spin" || key == "latency_spin") {
                g_spinPercent = std::clamp(std::atoi(valStr.c_str()), 0, 100);
            }
            else if (key == "debug_mode" || key == "debug") {
                if (!g_debugCliOverride) {
                    g_debugMode = (std::atoi(valStr.c_str()) != 0);
                }
            }
            else if (key == "palette" || key == "theme") { g_activePalette = valStr; }
            else if (key == "bg_image" || key == "background" || key == "wallpaper") { g_bgImagePath = valStr; }
            else if (key == "copy_bg_image" || key == "copy_bg") { g_copyBgImage = (std::atoi(valStr.c_str()) != 0); }
            else if (key == "delete_old_bg_image" || key == "delete_old_bg") { g_deleteOldBgImage = (std::atoi(valStr.c_str()) != 0); }
            else if (key == "copied_bg_images") {
                g_copiedBgImages.clear();
                std::string s = valStr;
                size_t start = 0;
                while (start < s.size()) {
                    size_t sc = s.find(';', start);
                    std::string item = (sc == std::string::npos) ? s.substr(start) : s.substr(start, sc - start);
                    item = Trim(item);
                    if (!item.empty()) g_copiedBgImages.push_back(item);
                    if (sc == std::string::npos) break;
                    start = sc + 1;
                }
            }
            else if (key == "ui_opacity" || key == "opacity") { int op = std::atoi(valStr.c_str()); if (op >= 10 && op <= 100) g_uiOpacity = op; }
            else if (key == "bg_blur" || key == "blur") { int bl = std::atoi(valStr.c_str()); if (bl >= 0 && bl <= 20) g_bgBlur = bl; }
            else if (key == "active_tab" || key == "tab") { int t = std::atoi(valStr.c_str()); if (t >= 0 && t <= 1) g_activeTab = t; }
            else if (key == "active_preset" || key == "preset") { g_activePresetName = valStr; }
        }
    }
    flushSection();

    g_target_monitor_idx = mon;

    if (!has_w || !has_h || !has_cx || !has_cy || w <= 0 || h <= 0 || cx <= 0 || cy <= 0) {
        SetDefaults();
    }
    else {
        SetTransform(w, h, cx, cy, rot, mon);
    }

    g_configRatio = ratioStr;
    if (hRatio) SetWindowTextA(hRatio, g_configRatio.c_str());
    if (chkRatio) InvalidateRect(chkRatio, NULL, FALSE);
    if (chkLockDrag) InvalidateRect(chkLockDrag, NULL, FALSE);
    if (chkClipArea) InvalidateRect(chkClipArea, NULL, FALSE);
    if (chkPenClick) InvalidateRect(chkPenClick, NULL, FALSE);
    if (chkAntiAlias) InvalidateRect(chkAntiAlias, NULL, FALSE);
    if (chkDebugMode) InvalidateRect(chkDebugMode, NULL, FALSE);
    if (chkExtrapolate) InvalidateRect(chkExtrapolate, NULL, FALSE);

    if (g_presets.empty()) {
        g_presets.push_back(CreateDefaultPreset());
    }
    if (g_activePresetName.empty()) {
        g_activePresetName = "Default";
    }

    SetupDebugConsole(g_debugMode);
    g_specStore.Store(g_spec.ToRuntimeSpec());

    UpdateStatusText();

    if (g_guiReady && g_hwnd) {
        UpdateSystemAccentPalette();
        RefreshPaletteCombo();
        RefreshPresetCombo();
        UpdateOpacityDisplay();
        UpdateBlurDisplay();
        UpdateExtrapolateDisplay();
        UpdateSpinDisplay();
        ApplyBgBlur();
        InvalidateRect(g_hwnd, NULL, TRUE);
    }
}

void CheckConfigFileReload() {
    std::string cfgPath = g_baseDir + "config.ini";
    std::wstring wCfgPath = Utf8ToUtf16(cfgPath);
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(wCfgPath.c_str(), GetFileExInfoStandard, &fad)) {
        if (fad.ftLastWriteTime.dwLowDateTime != g_lastConfigTime.dwLowDateTime ||
            fad.ftLastWriteTime.dwHighDateTime != g_lastConfigTime.dwHighDateTime) {

            g_lastConfigTime = fad.ftLastWriteTime;
            LoadConfig();
        }
    }
}

void SavePreset(const std::string& name) {
    if (name.empty()) return;
    TransformConfig cfg = g_cfgStore.Load();
    char rBuf[32] = "";
    if (hRatio) GetWindowTextA(hRatio, rBuf, sizeof(rBuf));
    else strncpy_s(rBuf, sizeof(rBuf), g_configRatio.c_str(), _TRUNCATE);

    auto it = std::ranges::find_if(g_presets, [&](const auto& p) {
        return _stricmp(p.name.c_str(), name.c_str()) == 0;
    });
    AreaPreset& p = (it != g_presets.end()) ? *it : g_presets.emplace_back();
    p.name = name;
    p.width_mm = cfg.width_mm;
    p.height_mm = cfg.height_mm;
    p.cx = cfg.cx;
    p.cy = cfg.cy;
    p.rotation_deg = cfg.rotation_deg;
    p.ratio = rBuf;
    p.monitor = g_target_monitor_idx;
    p.palette = g_activePalette;
    p.bg_image = g_bgImagePath;
    g_activePresetName = name;
    SaveConfig();
    RefreshPresetCombo();
}

void DeletePreset(const std::string& name) {
    if (name.empty() || _stricmp(name.c_str(), "Default") == 0) return;
    std::erase_if(g_presets, [&](const auto& p) {
        return _stricmp(p.name.c_str(), name.c_str()) == 0;
    });
    if (g_presets.empty()) {
        g_presets.push_back(CreateDefaultPreset());
    }
    if (_stricmp(g_activePresetName.c_str(), name.c_str()) == 0) {
        g_activePresetName = g_presets[0].name;
        ApplyPreset(g_activePresetName);
    } else {
        SaveConfig();
        RefreshPresetCombo();
    }
}

bool ApplyPreset(const std::string& name) {
    auto it = std::ranges::find_if(g_presets, [&](const auto& p) {
        return _stricmp(p.name.c_str(), name.c_str()) == 0;
    });
    if (it == g_presets.end()) return false;
    const auto& p = *it;

    g_activePresetName = p.name;
    g_target_monitor_idx = p.monitor;
    g_configRatio = p.ratio;

    SetTransform(p.width_mm, p.height_mm, p.cx, p.cy, p.rotation_deg, p.monitor);

    if (!p.palette.empty() && p.palette != g_activePalette) {
        g_activePalette = p.palette;
        UpdateSystemAccentPalette();
        RefreshPaletteCombo();
    }
    if (p.bg_image != g_bgImagePath) {
        g_bgImagePath = p.bg_image;
        LoadBackgroundImage(g_bgImagePath);
    }

    auto setWinText = [](HWND h, const std::string& val) {
        if (h) SetWindowTextA(h, val.c_str());
    };
    g_isUpdatingUI = true;
    setWinText(hW, FormatDouble(p.width_mm));
    setWinText(hH, FormatDouble(p.height_mm));
    setWinText(hCX, FormatDouble(p.cx));
    setWinText(hCY, FormatDouble(p.cy));
    setWinText(hRot, FormatDouble(p.rotation_deg));
    setWinText(hRatio, p.ratio);
    if (hCboMon) {
        int idx = (g_target_monitor_idx >= 0 && g_target_monitor_idx < static_cast<int>(g_monitors.size()))
            ? g_target_monitor_idx : static_cast<int>(g_monitors.size());
        SendMessage(hCboMon, CB_SETCURSEL, idx, 0);
    }
    g_isUpdatingUI = false;

    SaveConfig();
    RefreshPresetCombo();
    if (g_hwnd) {
        InvalidateRect(g_hwnd, NULL, FALSE);
    }
    return true;
}

void SaveCustomPalette(const std::string& name, COLORREF color, const std::string& bg_image) {
    std::string cleanName = Trim(name);
    cleanName.erase(std::remove_if(cleanName.begin(), cleanName.end(), [](char c) {
        return c == '[' || c == ']' || c == '\r' || c == '\n';
    }), cleanName.end());
    if (cleanName.empty()) return;

    auto it = std::ranges::find_if(g_customPalettes, [&](const auto& cp) {
        return _stricmp(cp.name.c_str(), cleanName.c_str()) == 0;
    });
    if (it != g_customPalettes.end()) {
        it->color = color;
        it->bg_image = bg_image;
    } else {
        g_customPalettes.push_back({ cleanName, color, bg_image });
    }
    SaveConfig();
}

void DeleteCustomPalette(const std::string& name) {
    if (name.empty()) return;
    if (std::erase_if(g_customPalettes, [&](const auto& cp) {
        return _stricmp(cp.name.c_str(), name.c_str()) == 0;
    }) > 0) {
        if (_stricmp(g_activePalette.c_str(), name.c_str()) == 0) {
            g_activePalette = "system";
            UpdateSystemAccentPalette();
        }
        SaveConfig();
    }
}


