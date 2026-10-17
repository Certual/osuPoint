#pragma once
#include "common.h"
#include "tablet_spec.h"

struct DisplayMonitor {
    int index = 0;
    std::string name;
    RECT rc{};
    bool isPrimary = false;
};

extern std::vector<DisplayMonitor> g_monitors;
extern int g_target_monitor_idx;
extern int g_disp_w;
extern int g_disp_h;
extern int g_disp_x;
extern int g_disp_y;

extern FILETIME g_lastConfigTime;
extern std::string g_baseDir;

extern bool g_lockRatio;
extern std::string g_configRatio;
extern int g_ratioPrecision;
extern bool g_lockDrag;

struct AreaPreset {
    std::string name;
    double width_mm = 0;
    double height_mm = 0;
    double cx = 0;
    double cy = 0;
    double rotation_deg = 0;
    std::string ratio = "16:9";
    int monitor = 0;
    std::string palette;
    std::string bg_image;
};

struct CustomPalette {
    std::string name;
    COLORREF color = 0;
    std::string bg_image;
};

extern std::vector<AreaPreset> g_presets;
extern std::string g_activePresetName;
extern bool g_antialiasing;
extern bool g_extrapolation;
extern double g_extrapolationTau;
extern int g_spinPercent;
extern std::string g_activePalette;
extern std::string g_bgImagePath;
extern bool g_copyBgImage;
extern bool g_deleteOldBgImage;
extern std::vector<std::string> g_copiedBgImages;
extern int g_uiOpacity;
extern int g_bgBlur;
extern int g_activeTab;
extern std::vector<CustomPalette> g_customPalettes;

std::string Trim(const std::string& str) noexcept;
std::string ToLower(std::string str) noexcept;
std::string Utf16ToUtf8(const std::wstring& wstr);
std::wstring Utf8ToUtf16(const std::string& str);
std::string GetExeDirectory();
USHORT ParseHexOrDec(const std::string& str) noexcept;
double ParseAspectRatio(const std::string& str);
double ParseInput(HWND hEdit);
std::string FormatDouble(double v);
std::string FormatRatio(double r, int precision);
void ParseHexBytes(const char* hexStr, std::vector<uint8_t>& outBytes);
void ApplyBuiltinSpec(const BuiltinTabletDef& def, TabletSpec& spec);

void RefreshMonitors();
RECT GetTargetMonitorRect(int selectedIndex);

extern std::vector<TabletSpec> g_cachedSpecs;
extern bool g_specsCached;

bool LoadTabletProfile(const std::wstring& filepath, TabletSpec& spec);
void InitAllTabletSpecs();

void SetDefaults() noexcept;
void SaveConfig();
void LoadConfig();
void CheckConfigFileReload();

void SavePreset(const std::string& name);
void DeletePreset(const std::string& name);
bool ApplyPreset(const std::string& name);

void SaveCustomPalette(const std::string& name, COLORREF color, const std::string& bg_image = "");
void DeleteCustomPalette(const std::string& name);


