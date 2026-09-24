#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <dbt.h>
#include <timeapi.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <avrt.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <immintrin.h>

#include <thread>
#include <atomic>
#include <algorithm>
#include <string>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "avrt.lib")

constexpr double PI = 3.14159265358979323846;
constexpr UINT WM_TRAYICON = WM_USER + 1;
constexpr UINT IDM_TRAY_OPEN = 2001;
constexpr UINT IDM_TRAY_EXIT = 2002;

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

typedef NTSTATUS(NTAPI* pfnNtSetTimerResolution)(ULONG DesiredResolution, BOOLEAN SetResolution, PULONG CurrentResolution);
typedef BOOL(WINAPI* pfnSetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

#ifndef ThreadPowerThrottling
#define ThreadPowerThrottling static_cast<THREAD_INFORMATION_CLASS>(1)
#endif

#ifndef ProcessPowerThrottling
#define ProcessPowerThrottling static_cast<PROCESS_INFORMATION_CLASS>(4)
#endif

#ifndef THREAD_POWER_THROTTLING_CURRENT_VERSION
typedef struct _THREAD_POWER_THROTTLING_STATE {
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
} THREAD_POWER_THROTTLING_STATE, *PTHREAD_POWER_THROTTLING_STATE;
#define THREAD_POWER_THROTTLING_CURRENT_VERSION 1
#define THREAD_POWER_THROTTLING_EXECUTION_SPEED 1
#endif

#ifndef PROCESS_POWER_THROTTLING_CURRENT_VERSION
typedef struct _PROCESS_POWER_THROTTLING_STATE {
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
} PROCESS_POWER_THROTTLING_STATE, *PPROCESS_POWER_THROTTLING_STATE;
#define PROCESS_POWER_THROTTLING_CURRENT_VERSION 1
#define PROCESS_POWER_THROTTLING_EXECUTION_SPEED 1
#endif

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

const COLORREF COLOR_BG         = RGB(20, 20, 24);
const COLORREF COLOR_CARD       = RGB(28, 28, 34);
const COLORREF COLOR_EDIT_BG    = RGB(36, 36, 44);
const COLORREF COLOR_TEXT       = RGB(230, 230, 235);
const COLORREF COLOR_TEXT_MUTED = RGB(140, 140, 155);
const COLORREF COLOR_ACCENT     = RGB(255, 102, 170); // osu! Pink
const COLORREF COLOR_BTN        = RGB(38, 38, 48);
const COLORREF COLOR_BTN_HOVER  = RGB(52, 52, 66);
const COLORREF COLOR_BORDER     = RGB(55, 55, 68);

struct TabletSpec {
    std::string name = "Wacom One CTL-472";
    USHORT vid = 0x056A;
    USHORT pid = 0x037A;
    int max_x = 15200;
    int max_y = 9500;
    double phys_w = 152.0;
    double phys_h = 95.0;
    int report_len = 10;
    int report_id = 0x02;
    int x_offset = 2;
    int y_offset = 4;
    std::vector<BYTE> init_feature{ 0x02, 0x02 };
};

struct DisplayMonitor {
    int index = 0;
    std::string name;
    RECT rc{};
    bool isPrimary = false;
};

struct alignas(64) TransformConfig {
    // Folded matrix coefficients for multi-monitor virtual desktop:
    // out_x = raw_x * k00 + raw_y * k01 + k02
    // out_y = raw_x * k10 + raw_y * k11 + k12
    double k00 = 0.0;
    double k01 = 0.0;
    double k02 = 0.0;
    double k10 = 0.0;
    double k11 = 0.0;
    double k12 = 0.0;

    double width_mm = 152.0;
    double height_mm = 95.0;
    double cx = 76.0;
    double cy = 47.5;
    double rotation_deg = 0.0;
    int monitor_idx = 0;
};

class AtomicConfigStore {
public:
    void Store(const TransformConfig& newCfg) noexcept {
        uint32_t s = m_seq.load(std::memory_order_relaxed);
        m_seq.store(s + 1, std::memory_order_release);
        m_cfg = newCfg;
        m_seq.store(s + 2, std::memory_order_release);
    }

    TransformConfig Load() const noexcept {
        TransformConfig result;
        uint32_t s1 = 0, s2 = 0;
        do {
            s1 = m_seq.load(std::memory_order_acquire);
            while (s1 & 1) {
                _mm_pause();
                s1 = m_seq.load(std::memory_order_acquire);
            }
            result = m_cfg;
            s2 = m_seq.load(std::memory_order_acquire);
        } while (s1 != s2);
        return result;
    }

private:
    std::atomic<uint32_t> m_seq{ 0 };
    TransformConfig m_cfg{};
};

TabletSpec g_spec;
AtomicConfigStore g_cfgStore;

std::vector<DisplayMonitor> g_monitors;
int g_target_monitor_idx = 0;
int g_disp_w = 0;
int g_disp_h = 0;
int g_disp_x = -1;
int g_disp_y = -1;

FILETIME g_lastConfigTime = { 0 };
HANDLE g_hDevice = INVALID_HANDLE_VALUE;
HWND g_hwnd = NULL;
bool g_guiReady = false;
bool g_isUpdatingUI = false;
std::string g_baseDir = "";

HANDLE g_hExitEvent = NULL;
HANDLE g_hDeviceChangeEvent = NULL;
HDEVNOTIFY g_hDevNotify = NULL;

std::thread g_driverThread;

HFONT g_hFont = NULL;
HFONT g_hFontSmall = NULL;
HBRUSH g_hBrushBg = NULL;
HBRUSH g_hBrushEdit = NULL;
NOTIFYICONDATAA g_nid = { 0 };

HWND hCboMon = NULL;
HWND hW = NULL, hH = NULL, hCX = NULL, hCY = NULL, hRot = NULL;
HWND chkRatio = NULL, hRatio = NULL, btnAutoRatio = NULL;

// Preview canvas layout
constexpr int PREVIEW_X = 230;
constexpr int PREVIEW_Y = 12;
constexpr int PREVIEW_MAX_W = 300;
constexpr int PREVIEW_MAX_H = 220;
constexpr int BEZEL = 7;

int g_originX = 0;
int g_originY = 0;
double g_previewScale = 1.0;
POINT g_previewCorners[4];
POINT g_previewCenter;

enum class DragMode { None, Move, ResizeCorner0, ResizeCorner1, ResizeCorner2, ResizeCorner3 };
DragMode g_dragMode = DragMode::None;
POINT g_dragStartMouse{};
double g_dragOrigW = 0, g_dragOrigH = 0, g_dragOrigCX = 0, g_dragOrigCY = 0;

inline std::string Trim(const std::string& str) noexcept {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

inline std::string ToLower(std::string str) noexcept {
    for (char& c : str) {
        if (c >= 'A' && c <= 'Z') c += ('a' - 'A');
    }
    return str;
}

void PreciseSleep(DWORD ms) noexcept {
    static HANDLE hTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (hTimer) {
        LARGE_INTEGER due;
        due.QuadPart = -static_cast<LONGLONG>(ms) * 10000LL;
        SetWaitableTimer(hTimer, &due, 0, nullptr, nullptr, FALSE);
        WaitForSingleObject(hTimer, INFINITE);
    } else {
        Sleep(ms);
    }
}

void EnablePerMonitorDpi() noexcept {
    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (hUser32) {
        auto pSetDpi = reinterpret_cast<pfnSetProcessDpiAwarenessContext>(GetProcAddress(hUser32, "SetProcessDpiAwarenessContext"));
        if (pSetDpi) {
            pSetDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }
}

void EnableSubMillisecondTimer() noexcept {
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (hNtdll) {
        auto NtSetTimerResolution = reinterpret_cast<pfnNtSetTimerResolution>(GetProcAddress(hNtdll, "NtSetTimerResolution"));
        if (NtSetTimerResolution) {
            ULONG currentRes = 0;
            if (NtSetTimerResolution(5000, TRUE, &currentRes) == 0) {
                return;
            }
        }
    }
    timeBeginPeriod(1);
}

void DisableSubMillisecondTimer() noexcept {
    timeEndPeriod(1);
}

void DisablePowerThrottling() noexcept {
    PROCESS_POWER_THROTTLING_STATE ppts{};
    ppts.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    ppts.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    ppts.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &ppts, sizeof(ppts));

    THREAD_POWER_THROTTLING_STATE tpts{};
    tpts.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    tpts.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    tpts.StateMask = 0;
    SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &tpts, sizeof(tpts));
}

DWORD_PTR CalculateOptimalAffinity() noexcept {
    DWORD_PTR processAffinity = 0, systemAffinity = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &processAffinity, &systemAffinity) || processAffinity == 0) {
        return 0;
    }

    DWORD returnLength = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &returnLength);
    if (returnLength > 0) {
        std::vector<BYTE> buffer(returnLength);
        if (GetLogicalProcessorInformationEx(RelationProcessorCore, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &returnLength)) {
            BYTE maxEfficiency = 0;
            DWORD_PTR pCoreMask = 0;
            BYTE* ptr = buffer.data();
            BYTE* end = ptr + returnLength;

            while (ptr < end) {
                auto current = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(ptr);
                if (current->Relationship == RelationProcessorCore) {
                    if (current->Processor.EfficiencyClass > maxEfficiency) {
                        maxEfficiency = current->Processor.EfficiencyClass;
                    }
                }
                ptr += current->Size;
            }

            ptr = buffer.data();
            while (ptr < end) {
                auto current = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(ptr);
                if (current->Relationship == RelationProcessorCore) {
                    if (current->Processor.EfficiencyClass == maxEfficiency && current->Processor.GroupCount > 0) {
                        pCoreMask |= current->Processor.GroupMask[0].Mask;
                    }
                }
                ptr += current->Size;
            }

            DWORD_PTR validPCores = (pCoreMask & processAffinity) & ~static_cast<DWORD_PTR>(1ULL);
            if (validPCores != 0) {
                unsigned long index = 0;
#if defined(_M_X64) || defined(__x86_64__)
                if (_BitScanForward64(&index, validPCores)) return (1ULL << index);
#else
                if (_BitScanForward(&index, static_cast<unsigned long>(validPCores))) return (1ULL << index);
#endif
            }
        }
    }

    DWORD_PTR nonCore0 = processAffinity & ~static_cast<DWORD_PTR>(1ULL);
    if (nonCore0 != 0) {
        unsigned long index = 0;
#if defined(_M_X64) || defined(__x86_64__)
        if (_BitScanForward64(&index, nonCore0)) return (1ULL << index);
#else
        if (_BitScanForward(&index, static_cast<unsigned long>(nonCore0))) return (1ULL << index);
#endif
    }
    return processAffinity;
}

std::string GetExeDirectory() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    return std::string(exePath);
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

BOOL CALLBACK MonitorEnumProc(HMONITOR hMon, HDC, LPRECT, LPARAM) {
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

bool LoadTabletProfile(const std::string& filepath, TabletSpec& spec) {
    std::ifstream f(filepath);
    if (!f.is_open()) return false;

    std::string line;
    spec.init_feature.clear();

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
        else if (key == "report_id") spec.report_id = ParseHexOrDec(val);
        else if (key == "x_offset") spec.x_offset = std::atoi(val.c_str());
        else if (key == "y_offset") spec.y_offset = std::atoi(val.c_str());
        else if (key == "init_feature") {
            const char* p = val.c_str();
            while (*p) {
                while (*p == ' ' || *p == '\t' || *p == ',') ++p;
                if (!*p) break;
                char* endP = nullptr;
                unsigned long b = std::strtoul(p, &endP, 0);
                if (p == endP) break;
                spec.init_feature.push_back(static_cast<BYTE>(b));
                p = endP;
            }
        }
    }
    return true;
}

void EnsureDefaultTabletProfile(const std::string& folder) {
    CreateDirectoryA(folder.c_str(), NULL);
    std::string defFile = folder + "\\Wacom_CTL-472.cfg";
    std::ifstream check(defFile);
    if (!check.is_open()) {
        std::ofstream f(defFile);
        f << "# osu!Point Tablet Profile\n"
          << "name=Wacom One CTL-472\n"
          << "vid=0x056A\n"
          << "pid=0x037A\n"
          << "max_x=15200\n"
          << "max_y=9500\n"
          << "width_mm=152.0\n"
          << "height_mm=95.0\n"
          << "report_len=10\n"
          << "report_id=0x02\n"
          << "x_offset=2\n"
          << "y_offset=4\n"
          << "init_feature=0x02 0x02\n";
    }
}

std::string GetDevicePath(USHORT vid, USHORT pid, int targetReportLen) {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO devInfo = SetupDiGetClassDevsA(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return "";

    SP_DEVICE_INTERFACE_DATA devData{ sizeof(SP_DEVICE_INTERFACE_DATA) };
    std::string foundPath = "";

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &devData); ++i) {
        DWORD reqSize = 0;
        SetupDiGetDeviceInterfaceDetailA(devInfo, &devData, nullptr, 0, &reqSize, nullptr);
        auto detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_A>(malloc(reqSize));
        if (!detail) continue;
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        if (SetupDiGetDeviceInterfaceDetailA(devInfo, &devData, detail, reqSize, nullptr, nullptr)) {
            HANDLE h = CreateFileA(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                HIDD_ATTRIBUTES attr{ sizeof(HIDD_ATTRIBUTES) };
                if (HidD_GetAttributes(h, &attr)) {
                    if (attr.VendorID == vid && attr.ProductID == pid) {
                        PHIDP_PREPARSED_DATA ppData = nullptr;
                        if (HidD_GetPreparsedData(h, &ppData)) {
                            HIDP_CAPS caps;
                            if (HidP_GetCaps(ppData, &caps) == HIDP_STATUS_SUCCESS) {
                                if (caps.InputReportByteLength == targetReportLen || caps.InputReportByteLength == targetReportLen + 1) {
                                    foundPath = detail->DevicePath;
                                    HidD_FreePreparsedData(ppData);
                                    CloseHandle(h);
                                    free(detail);
                                    break;
                                }
                            }
                            HidD_FreePreparsedData(ppData);
                        }
                        if (foundPath.empty() && strstr(detail->DevicePath, "&mi_00") != nullptr) {
                            foundPath = detail->DevicePath;
                        }
                    }
                }
                CloseHandle(h);
            }
        }
        free(detail);
    }
    SetupDiDestroyDeviceInfoList(devInfo);
    return foundPath;
}

void SetTransform(double w, double h, double cx, double cy, double rot_deg, int monitor_idx) noexcept {
    TransformConfig cfg;
    cfg.width_mm = w;
    cfg.height_mm = h;
    cfg.cx = cx;
    cfg.cy = cy;
    cfg.rotation_deg = rot_deg;
    cfg.monitor_idx = monitor_idx;

    const double inv_scale_x = g_spec.phys_w / static_cast<double>(g_spec.max_x);
    const double inv_scale_y = g_spec.phys_h / static_cast<double>(g_spec.max_y);

    const double rad = (-rot_deg) * (PI / 180.0);
    const double cos_val = std::cos(rad);
    const double sin_val = std::sin(rad);

    const double a = cos_val / w;
    const double b = -sin_val / w;
    const double c = sin_val / h;
    const double d = cos_val / h;

    RECT rcMon = GetTargetMonitorRect(monitor_idx);
    int mon_w = rcMon.right - rcMon.left;
    int mon_h = rcMon.bottom - rcMon.top;

    double eff_w = (g_disp_w > 0) ? static_cast<double>(g_disp_w) : static_cast<double>(mon_w);
    double eff_h = (g_disp_h > 0) ? static_cast<double>(g_disp_h) : static_cast<double>(mon_h);

    eff_w = (std::min)(static_cast<double>(mon_w), (std::max)(2.0, eff_w));
    eff_h = (std::min)(static_cast<double>(mon_h), (std::max)(2.0, eff_h));

    double off_x = (g_disp_x >= 0) ? static_cast<double>(g_disp_x) : (static_cast<double>(mon_w) - eff_w) * 0.5;
    double off_y = (g_disp_y >= 0) ? static_cast<double>(g_disp_y) : (static_cast<double>(mon_h) - eff_h) * 0.5;

    double target_l = static_cast<double>(rcMon.left) + off_x;
    double target_t = static_cast<double>(rcMon.top) + off_y;

    double x_virt = static_cast<double>(GetSystemMetrics(SM_XVIRTUALSCREEN));
    double y_virt = static_cast<double>(GetSystemMetrics(SM_YVIRTUALSCREEN));
    double w_virt = static_cast<double>(GetSystemMetrics(SM_CXVIRTUALSCREEN));
    double h_virt = static_cast<double>(GetSystemMetrics(SM_CYVIRTUALSCREEN));

    double denom_x = (w_virt > 1.0) ? (w_virt - 1.0) : 1.0;
    double denom_y = (h_virt > 1.0) ? (h_virt - 1.0) : 1.0;

    double scale_x = (eff_w - 1.0) * 65535.0 / denom_x;
    double shift_x = (target_l - x_virt) * 65535.0 / denom_x;

    double scale_y = (eff_h - 1.0) * 65535.0 / denom_y;
    double shift_y = (target_t - y_virt) * 65535.0 / denom_y;

    cfg.k00 = (inv_scale_x * a) * scale_x;
    cfg.k01 = (inv_scale_y * b) * scale_x;
    cfg.k02 = (0.5 - (cx * a + cy * b)) * scale_x + shift_x;

    cfg.k10 = (inv_scale_x * c) * scale_y;
    cfg.k11 = (inv_scale_y * d) * scale_y;
    cfg.k12 = (0.5 - (cx * c + cy * d)) * scale_y + shift_y;

    g_cfgStore.Store(cfg);
}

std::string DetectAndInitTablet() {
    std::string tabletsDir = g_baseDir + "tablets";
    EnsureDefaultTabletProfile(tabletsDir);

    std::string searchPath = tabletsDir + "\\*.cfg";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            TabletSpec candidate;
            std::string fullPath = tabletsDir + "\\" + fd.cFileName;
            if (LoadTabletProfile(fullPath, candidate)) {
                std::string devPath = GetDevicePath(candidate.vid, candidate.pid, candidate.report_len);
                if (!devPath.empty()) {
                    g_spec = candidate;
                    FindClose(hFind);
                    return devPath;
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }

    LoadTabletProfile(tabletsDir + "\\Wacom_CTL-472.cfg", g_spec);
    return GetDevicePath(g_spec.vid, g_spec.pid, g_spec.report_len);
}

HANDLE OpenTabletHandle(const std::string& devPath) noexcept {
    HANDLE hDevice = CreateFileA(devPath.c_str(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);

    if (hDevice == INVALID_HANDLE_VALUE) {
        hDevice = CreateFileA(devPath.c_str(), GENERIC_READ,
            0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    }
    if (hDevice == INVALID_HANDLE_VALUE) {
        hDevice = CreateFileA(devPath.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    }
    if (hDevice == INVALID_HANDLE_VALUE) {
        hDevice = CreateFileA(devPath.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    }
    return hDevice;
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

void SaveConfig() {
    if (!g_guiReady) return;
    TransformConfig cfg = g_cfgStore.Load();
    std::string cfgPath = g_baseDir + "config.ini";
    std::ofstream f(cfgPath);
    if (!f.is_open()) return;

    char rBuf[32];
    GetWindowTextA(hRatio, rBuf, 32);
    bool locked = (SendMessage(chkRatio, BM_GETCHECK, 0, 0) == BST_CHECKED);

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
    f << "lock_ratio=" << (locked ? 1 : 0) << "\n";
    f << "ratio=" << rBuf << "\n";
    f.close();

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExA(cfgPath.c_str(), GetFileExInfoStandard, &fad)) {
        g_lastConfigTime = fad.ftLastWriteTime;
    }
}

void SetDefaults() noexcept {
    SetTransform(g_spec.phys_w, g_spec.phys_h, g_spec.phys_w * 0.5, g_spec.phys_h * 0.5, 0.0, 0);
}

void UpdateValues();

void UpdateStatusText() noexcept {
    if (!g_hwnd) return;
    RECT rc = GetTargetMonitorRect(g_target_monitor_idx);
    int cur_w = (g_disp_w > 0) ? g_disp_w : (rc.right - rc.left);
    int cur_h = (g_disp_h > 0) ? g_disp_h : (rc.bottom - rc.top);

    std::string title = "osu!Point - [" + g_spec.name + "] - [" + std::to_string(cur_w) + "x" + std::to_string(cur_h) + "]";
    SetWindowTextA(g_hwnd, title.c_str());
    strncpy_s(g_nid.szTip, title.c_str(), sizeof(g_nid.szTip) - 1);
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);
}

void LoadConfig() {
    std::string cfgPath = g_baseDir + "config.ini";
    std::ifstream f(cfgPath);
    if (!f.is_open()) {
        SetDefaults();
        return;
    }

    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExA(cfgPath.c_str(), GetFileExInfoStandard, &fad)) {
        g_lastConfigTime = fad.ftLastWriteTime;
    }

    std::string line;
    double w = 0, h = 0, cx = 0, cy = 0, rot = 0;
    int mon = 0;
    bool has_w = false, has_h = false, has_cx = false, has_cy = false;
    std::string ratioStr = "16:9";
    bool locked = false;

    while (std::getline(f, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || (line.size() >= 2 && line[0] == '/' && line[1] == '/')) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = ToLower(Trim(line.substr(0, eq)));
        std::string valStr = Trim(line.substr(eq + 1));
        double val = std::atof(valStr.c_str());

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
        else if (key == "lock_ratio") { locked = (std::atoi(valStr.c_str()) != 0); }
        else if (key == "ratio") { ratioStr = valStr; }
    }

    g_target_monitor_idx = mon;

    if (!has_w || !has_h || !has_cx || !has_cy || w <= 0 || h <= 0 || cx <= 0 || cy <= 0) {
        SetDefaults();
    } else {
        SetTransform(w, h, cx, cy, rot, mon);
    }

    if (hRatio) SetWindowTextA(hRatio, ratioStr.c_str());
    if (chkRatio) SendMessage(chkRatio, BM_SETCHECK, locked ? BST_CHECKED : BST_UNCHECKED, 0);

    UpdateStatusText();
}

void CheckConfigFileReload() {
    std::string cfgPath = g_baseDir + "config.ini";
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExA(cfgPath.c_str(), GetFileExInfoStandard, &fad)) {
        if (fad.ftLastWriteTime.dwLowDateTime != g_lastConfigTime.dwLowDateTime ||
            fad.ftLastWriteTime.dwHighDateTime != g_lastConfigTime.dwHighDateTime) {

            g_lastConfigTime = fad.ftLastWriteTime;
            LoadConfig();

            TransformConfig current = g_cfgStore.Load();
            g_isUpdatingUI = true;
            if (hW) SetWindowTextA(hW, FormatDouble(current.width_mm).c_str());
            if (hH) SetWindowTextA(hH, FormatDouble(current.height_mm).c_str());
            if (hCX) SetWindowTextA(hCX, FormatDouble(current.cx).c_str());
            if (hCY) SetWindowTextA(hCY, FormatDouble(current.cy).c_str());
            if (hRot) SetWindowTextA(hRot, FormatDouble(current.rotation_deg).c_str());
            g_isUpdatingUI = false;

            if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
        }
    }
}

inline uint16_t ReadLE16(const BYTE* ptr) noexcept {
    uint16_t val;
    std::memcpy(&val, ptr, sizeof(uint16_t));
    return val;
}

// Drain-to-Latest Dual-Buffer Driver Loop
void DriverThread() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    DWORD_PTR affinity = CalculateOptimalAffinity();
    if (affinity != 0) {
        SetThreadAffinityMask(GetCurrentThread(), affinity);
    }

    DisablePowerThrottling();
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);

    DWORD taskIndex = 0;
    HANDLE hMmcss = AvSetMmThreadCharacteristicsA("Pro Audio", &taskIndex);
    if (hMmcss) {
        AvSetMmThreadPriority(hMmcss, AVRT_PRIORITY_CRITICAL);
    }

    OVERLAPPED ov[2]{};
    ov[0].hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    ov[1].hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);

    BYTE reportBuf[2][64];
    DWORD bytesRead[2] = { 0, 0 };

    int activeIdx = 0;
    bool isPending = false;

    INPUT mouseInput = { 0 };
    mouseInput.type = INPUT_MOUSE;
    mouseInput.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;

    LONG lastDx = -1;
    LONG lastDy = -1;

    while (WaitForSingleObject(g_hExitEvent, 0) == WAIT_TIMEOUT) {
        if (g_hDevice == INVALID_HANDLE_VALUE) {
            std::string devPath = DetectAndInitTablet();
            if (devPath.empty()) {
                if (g_hwnd) SetWindowTextA(g_hwnd, "osu!Point - [Waiting for tablet...]");
                HANDLE waitArr[2] = { g_hExitEvent, g_hDeviceChangeEvent };
                WaitForMultipleObjects(2, waitArr, FALSE, 1000);
                continue;
            }

            g_hDevice = OpenTabletHandle(devPath);
            if (g_hDevice == INVALID_HANDLE_VALUE) {
                HANDLE waitArr[2] = { g_hExitEvent, g_hDeviceChangeEvent };
                WaitForMultipleObjects(2, waitArr, FALSE, 500);
                continue;
            }

            HidD_SetNumInputBuffers(g_hDevice, 2);
            HidD_FlushQueue(g_hDevice);

            if (!g_spec.init_feature.empty()) {
                HidD_SetFeature(g_hDevice, g_spec.init_feature.data(), static_cast<ULONG>(g_spec.init_feature.size()));
            }

            TransformConfig currentCfg = g_cfgStore.Load();
            SetTransform(currentCfg.width_mm, currentCfg.height_mm, currentCfg.cx, currentCfg.cy, currentCfg.rotation_deg, currentCfg.monitor_idx);

            activeIdx = 0;
            isPending = false;
            lastDx = -1;
            lastDy = -1;
            UpdateStatusText();
        }

        if (!isPending) {
            ResetEvent(ov[activeIdx].hEvent);
            bytesRead[activeIdx] = 0;
            BOOL readOk = ReadFile(g_hDevice, reportBuf[activeIdx], sizeof(reportBuf[activeIdx]), nullptr, &ov[activeIdx]);
            if (!readOk) {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                    isPending = true;
                } else {
                    CloseHandle(g_hDevice);
                    g_hDevice = INVALID_HANDLE_VALUE;
                    continue;
                }
            } else {
                GetOverlappedResult(g_hDevice, &ov[activeIdx], &bytesRead[activeIdx], FALSE);
                isPending = false;
            }
        }

        if (isPending) {
            HANDLE waitArr[3] = { g_hExitEvent, g_hDeviceChangeEvent, ov[activeIdx].hEvent };
            DWORD waitRes = WaitForMultipleObjects(3, waitArr, FALSE, 500);

            if (waitRes == WAIT_OBJECT_0) {
                CancelIoEx(g_hDevice, &ov[activeIdx]);
                break;
            }
            if (waitRes == WAIT_OBJECT_0 + 1) {
                CancelIoEx(g_hDevice, &ov[activeIdx]);
                CloseHandle(g_hDevice);
                g_hDevice = INVALID_HANDLE_VALUE;
                isPending = false;
                continue;
            }
            if (waitRes == WAIT_OBJECT_0 + 2) {
                if (!GetOverlappedResult(g_hDevice, &ov[activeIdx], &bytesRead[activeIdx], FALSE)) {
                    CloseHandle(g_hDevice);
                    g_hDevice = INVALID_HANDLE_VALUE;
                    isPending = false;
                    continue;
                }
                isPending = false;
            } else {
                continue;
            }
        }

        // Drain-to-Latest: Instant consumption of queued subsequent packets
        int latestIdx = activeIdx;
        while (true) {
            int nextIdx = 1 - latestIdx;
            ResetEvent(ov[nextIdx].hEvent);
            bytesRead[nextIdx] = 0;
            BOOL fastOk = ReadFile(g_hDevice, reportBuf[nextIdx], sizeof(reportBuf[nextIdx]), nullptr, &ov[nextIdx]);
            if (fastOk) {
                GetOverlappedResult(g_hDevice, &ov[nextIdx], &bytesRead[nextIdx], FALSE);
                if (bytesRead[nextIdx] > 0) {
                    latestIdx = nextIdx;
                }
            } else {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                    activeIdx = nextIdx;
                    isPending = true;
                }
                break;
            }
        }

        DWORD curBytes = bytesRead[latestIdx];
        if (curBytes == 0) continue;

        const BYTE* report = reportBuf[latestIdx];
        const int reportSize = static_cast<int>(curBytes);

        int raw_x = 0, raw_y = 0;
        const int xo = g_spec.x_offset;
        const int yo = g_spec.y_offset;
        const int req_id = g_spec.report_id;
        const int max_x = g_spec.max_x;
        const int max_y = g_spec.max_y;

        if (req_id == 0 || report[0] == req_id || report[0] == 0x10) {
            if (xo + 2 <= reportSize && yo + 2 <= reportSize) {
                raw_x = ReadLE16(&report[xo]);
                raw_y = ReadLE16(&report[yo]);
            }
        } else if (reportSize >= 6) {
            if (xo + 1 <= reportSize && yo + 1 <= reportSize) {
                raw_x = ReadLE16(&report[xo - 1]);
                raw_y = ReadLE16(&report[yo - 1]);
            }
        }

        if (raw_x == 0 && raw_y == 0) continue;
        if (raw_x > max_x || raw_y > max_y) continue;

        const TransformConfig cfg = g_cfgStore.Load();

        const double fx = static_cast<double>(raw_x);
        const double fy = static_cast<double>(raw_y);

        // Branchless folded FMA evaluation:
        const double out_x = std::fma(fx, cfg.k00, std::fma(fy, cfg.k01, cfg.k02));
        const double out_y = std::fma(fx, cfg.k10, std::fma(fy, cfg.k11, cfg.k12));

        const LONG targetDx = static_cast<LONG>(std::clamp(out_x, 0.0, 65535.0) + 0.5);
        const LONG targetDy = static_cast<LONG>(std::clamp(out_y, 0.0, 65535.0) + 0.5);

        if (targetDx == lastDx && targetDy == lastDy) {
            continue;
        }

        lastDx = targetDx;
        lastDy = targetDy;

        mouseInput.mi.dx = targetDx;
        mouseInput.mi.dy = targetDy;

        SendInput(1, &mouseInput, sizeof(INPUT));
    }

    if (g_hDevice != INVALID_HANDLE_VALUE) {
        CancelIoEx(g_hDevice, nullptr);
        CloseHandle(g_hDevice);
        g_hDevice = INVALID_HANDLE_VALUE;
    }

    CloseHandle(ov[0].hEvent);
    CloseHandle(ov[1].hEvent);

    if (hMmcss) {
        AvRevertMmThreadCharacteristics(hMmcss);
    }
}

void UpdateValues() {
    if (!g_guiReady || g_isUpdatingUI) return;

    double w = ParseInput(hW);
    double h = ParseInput(hH);
    double cx = ParseInput(hCX);
    double cy = ParseInput(hCY);
    double rot_deg = ParseInput(hRot);

    w = (std::max)(2.0, (std::min)(g_spec.phys_w, w));
    h = (std::max)(2.0, (std::min)(g_spec.phys_h, h));

    double rad = rot_deg * (PI / 180.0);
    double c = std::abs(std::cos(rad));
    double s = std::abs(std::sin(rad));

    double bbox_w = w * c + h * s;
    double bbox_h = w * s + h * c;

    if (bbox_w > g_spec.phys_w) {
        double sc = g_spec.phys_w / bbox_w;
        w *= sc; h *= sc;
        bbox_w = g_spec.phys_w;
        bbox_h *= sc;
    }
    if (bbox_h > g_spec.phys_h) {
        double sc = g_spec.phys_h / bbox_h;
        w *= sc; h *= sc;
        bbox_h = g_spec.phys_h;
        bbox_w *= sc;
    }

    double half_box_w = bbox_w * 0.5;
    double half_box_h = bbox_h * 0.5;

    cx = (std::max)(half_box_w, (std::min)(g_spec.phys_w - half_box_w, cx));
    cy = (std::max)(half_box_h, (std::min)(g_spec.phys_h - half_box_h, cy));

    SetTransform(w, h, cx, cy, rot_deg, g_target_monitor_idx);

    SaveConfig();
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
}

void AutoMatchRatio() {
    RECT rc = GetTargetMonitorRect(g_target_monitor_idx);
    int monW = (g_disp_w > 0) ? g_disp_w : (rc.right - rc.left);
    int monH = (g_disp_h > 0) ? g_disp_h : (rc.bottom - rc.top);

    auto gcd = [](int a, int b) {
        while (b) { int t = b; b = a % b; a = t; }
        return a;
    };
    int g = gcd(monW, monH);
    int rw = monW / g;
    int rh = monH / g;

    char buf[32];
    if (rw > 64 || rh > 64) {
        snprintf(buf, sizeof(buf), "%.3f", static_cast<double>(monW) / monH);
    } else {
        snprintf(buf, sizeof(buf), "%d:%d", rw, rh);
    }

    SetWindowTextA(hRatio, buf);
    SendMessage(chkRatio, BM_SETCHECK, BST_CHECKED, 0);

    g_isUpdatingUI = true;
    double ratio = ParseAspectRatio(buf);
    double curW = ParseInput(hW);
    double newH = curW / ratio;
    SetWindowTextA(hH, FormatDouble(newH).c_str());
    g_isUpdatingUI = false;

    UpdateValues();
}

HICON CreateAppIcon() {
    constexpr int size = 32;
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = -size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    DWORD* pPixels = nullptr;
    HBITMAP hbmColor = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, reinterpret_cast<void**>(&pPixels), NULL, 0);
    if (!hbmColor || !pPixels) return NULL;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float r_sum = 0, g_sum = 0, b_sum = 0, a_sum = 0;
            for (int sy = 0; sy < 4; ++sy) {
                for (int sx = 0; sx < 4; ++sx) {
                    float px = x + (sx + 0.5f) / 4.0f - 15.5f;
                    float py = y + (sy + 0.5f) / 4.0f - 15.5f;
                    float d = std::sqrt(px * px + py * py);

                    if (d <= 3.2f) {
                        r_sum += 255; g_sum += 255; b_sum += 255; a_sum += 255;
                    } else if (d <= 7.0f) {
                        r_sum += 28; g_sum += 28; b_sum += 34; a_sum += 255;
                    } else if (d <= 14.5f) {
                        r_sum += 255; g_sum += 102; b_sum += 170; a_sum += 255;
                    }
                }
            }

            BYTE a = static_cast<BYTE>(a_sum / 16.0f);
            BYTE r = static_cast<BYTE>(r_sum / 16.0f);
            BYTE g = static_cast<BYTE>(g_sum / 16.0f);
            BYTE b = static_cast<BYTE>(b_sum / 16.0f);

            r = static_cast<BYTE>((r * a) / 255);
            g = static_cast<BYTE>((g * a) / 255);
            b = static_cast<BYTE>((b * a) / 255);

            pPixels[y * size + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    HBITMAP hbmMask = CreateBitmap(size, size, 1, 1, NULL);

    ICONINFO ii = { 0 };
    ii.fIcon = TRUE;
    ii.hbmMask = hbmMask;
    ii.hbmColor = hbmColor;
    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hbmColor);
    DeleteObject(hbmMask);
    return hIcon;
}

int HitTestCanvas(int mx, int my) {
    auto distSq = [](POINT p, int x, int y) {
        int dx = p.x - x, dy = p.y - y;
        return dx * dx + dy * dy;
    };

    for (int i = 0; i < 4; ++i) {
        if (distSq(g_previewCorners[i], mx, my) <= 64) {
            return i;
        }
    }

    if (distSq(g_previewCenter, mx, my) <= 64) {
        return 4;
    }

    bool inside = false;
    for (int i = 0, j = 3; i < 4; j = i++) {
        if (((g_previewCorners[i].y > my) != (g_previewCorners[j].y > my)) &&
            (mx < (g_previewCorners[j].x - g_previewCorners[i].x) * (my - g_previewCorners[i].y) /
             (g_previewCorners[j].y - g_previewCorners[i].y) + g_previewCorners[i].x)) {
            inside = !inside;
        }
    }

    return inside ? 5 : -1;
}

void DrawPreview(HDC hdc) {
    int active_max_w = PREVIEW_MAX_W - 2 * BEZEL;
    int active_max_h = PREVIEW_MAX_H - 2 * BEZEL;

    g_previewScale = (std::min)(static_cast<double>(active_max_w) / g_spec.phys_w,
                                static_cast<double>(active_max_h) / g_spec.phys_h);

    int active_w = static_cast<int>(std::round(g_spec.phys_w * g_previewScale));
    int active_h = static_cast<int>(std::round(g_spec.phys_h * g_previewScale));

    int tab_w = active_w + 2 * BEZEL;
    int tab_h = active_h + 2 * BEZEL;

    g_originX = PREVIEW_X + BEZEL;
    g_originY = PREVIEW_Y + BEZEL;

    HBRUSH cardBrush = CreateSolidBrush(COLOR_CARD);
    HPEN borderPen = CreatePen(PS_SOLID, 1, COLOR_BORDER);
    HGDIOBJ oldBrush = SelectObject(hdc, cardBrush);
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    RoundRect(hdc, PREVIEW_X - 5, PREVIEW_Y - 5, PREVIEW_X + tab_w + 5, PREVIEW_Y + tab_h + 5, 8, 8);

    HBRUSH tabBrush = CreateSolidBrush(RGB(34, 34, 42));
    SelectObject(hdc, tabBrush);
    RoundRect(hdc, PREVIEW_X, PREVIEW_Y, PREVIEW_X + tab_w, PREVIEW_Y + tab_h, 6, 6);
    DeleteObject(tabBrush);

    TransformConfig cfg = g_cfgStore.Load();

    double rad = cfg.rotation_deg * (PI / 180.0);
    double c = std::cos(rad);
    double s = std::sin(rad);

    double hw = cfg.width_mm * 0.5;
    double hh = cfg.height_mm * 0.5;

    double local_corners[4][2] = {
        { -hw, -hh },
        {  hw, -hh },
        {  hw,  hh },
        { -hw,  hh }
    };

    for (int i = 0; i < 4; ++i) {
        double rx = cfg.cx + (local_corners[i][0] * c - local_corners[i][1] * s);
        double ry = cfg.cy + (local_corners[i][0] * s + local_corners[i][1] * c);
        g_previewCorners[i].x = g_originX + static_cast<int>(std::round(rx * g_previewScale));
        g_previewCorners[i].y = g_originY + static_cast<int>(std::round(ry * g_previewScale));
    }

    g_previewCenter.x = g_originX + static_cast<int>(std::round(cfg.cx * g_previewScale));
    g_previewCenter.y = g_originY + static_cast<int>(std::round(cfg.cy * g_previewScale));

    HRGN clipRgn = CreateRectRgn(g_originX - 2, g_originY - 2, g_originX + active_w + 3, g_originY + active_h + 3);
    SelectClipRgn(hdc, clipRgn);

    HBRUSH areaBrush = CreateSolidBrush(COLOR_ACCENT);
    HPEN areaPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    SelectObject(hdc, areaBrush);
    SelectObject(hdc, areaPen);

    Polygon(hdc, g_previewCorners, 4);

    HBRUSH handleBrush = CreateSolidBrush(RGB(255, 255, 255));
    SelectObject(hdc, handleBrush);
    SelectObject(hdc, GetStockObject(NULL_PEN));

    for (int i = 0; i < 4; ++i) {
        Ellipse(hdc, g_previewCorners[i].x - 3, g_previewCorners[i].y - 3, g_previewCorners[i].x + 4, g_previewCorners[i].y + 4);
    }

    Ellipse(hdc, g_previewCenter.x - 3, g_previewCenter.y - 3, g_previewCenter.x + 4, g_previewCenter.y + 4);

    DeleteObject(handleBrush);
    SelectClipRgn(hdc, NULL);
    DeleteObject(clipRgn);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(cardBrush);
    DeleteObject(borderPen);
    DeleteObject(areaBrush);
    DeleteObject(areaPen);
}

void ResetToFullArea() {
    SetDefaults();
    g_isUpdatingUI = true;
    SetWindowTextA(hW, FormatDouble(g_spec.phys_w).c_str());
    SetWindowTextA(hH, FormatDouble(g_spec.phys_h).c_str());
    SetWindowTextA(hCX, FormatDouble(g_spec.phys_w * 0.5).c_str());
    SetWindowTextA(hCY, FormatDouble(g_spec.phys_h * 0.5).c_str());
    SetWindowTextA(hRot, "0");
    g_isUpdatingUI = false;
    UpdateValues();
}

void RestoreWindow(HWND hwnd) {
    ShowWindow(hwnd, SW_SHOW);
    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
}

void ShutdownDriver() {
    static std::atomic<bool> s_shuttingDown{ false };
    if (s_shuttingDown.exchange(true)) return;

    if (g_hwnd) KillTimer(g_hwnd, 1);

    if (g_hExitEvent) {
        SetEvent(g_hExitEvent);
    }

    if (g_hDevNotify) {
        UnregisterDeviceNotification(g_hDevNotify);
        g_hDevNotify = NULL;
    }

    if (g_driverThread.joinable()) {
        g_driverThread.join();
    }

    DisableSubMillisecondTimer();
    SaveConfig();

    if (g_nid.hWnd) {
        Shell_NotifyIconA(NIM_DELETE, &g_nid);
        g_nid.hWnd = NULL;
    }

    if (g_hExitEvent) { CloseHandle(g_hExitEvent); g_hExitEvent = NULL; }
    if (g_hDeviceChangeEvent) { CloseHandle(g_hDeviceChangeEvent); g_hDeviceChangeEvent = NULL; }

    PostQuitMessage(0);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DEVICECHANGE) {
        if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
            if (g_hDeviceChangeEvent) {
                SetEvent(g_hDeviceChangeEvent);
            }
        }
        return TRUE;
    }
    if (msg == WM_DISPLAYCHANGE) {
        RefreshMonitors();
        UpdateValues();
        UpdateStatusText();
        return 0;
    }
    if (msg == WM_TIMER && wParam == 1) {
        CheckConfigFileReload();
        return 0;
    }
    if (msg == WM_ACTIVATE && LOWORD(wParam) != WA_INACTIVE) {
        CheckConfigFileReload();
    }
    if (msg == WM_SYSCOMMAND && (wParam & 0xFFF0) == SC_MINIMIZE) {
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    if (msg == WM_SIZE && wParam == SIZE_MINIMIZED) {
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }
    if (msg == WM_TRAYICON) {
        if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) {
            RestoreWindow(hwnd);
        } else if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            InsertMenuA(hMenu, 0, MF_BYPOSITION | MF_STRING, IDM_TRAY_OPEN, "Open osu!Point");
            InsertMenuA(hMenu, 1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
            InsertMenuA(hMenu, 2, MF_BYPOSITION | MF_STRING, IDM_TRAY_EXIT, "Exit");

            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        return 0;
    }

    if (msg == WM_SETCURSOR) {
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            int hit = HitTestCanvas(pt.x, pt.y);
            if (hit == 0 || hit == 2) {
                SetCursor(LoadCursor(NULL, IDC_SIZENWSE));
                return TRUE;
            } else if (hit == 1 || hit == 3) {
                SetCursor(LoadCursor(NULL, IDC_SIZENESW));
                return TRUE;
            } else if (hit == 4 || hit == 5) {
                SetCursor(LoadCursor(NULL, IDC_SIZEALL));
                return TRUE;
            }
        }
    }
    if (msg == WM_LBUTTONDOWN) {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);
        int hit = HitTestCanvas(mx, my);
        if (hit != -1) {
            SetCapture(hwnd);
            g_dragStartMouse = { mx, my };
            TransformConfig cfg = g_cfgStore.Load();
            g_dragOrigW = cfg.width_mm;
            g_dragOrigH = cfg.height_mm;
            g_dragOrigCX = cfg.cx;
            g_dragOrigCY = cfg.cy;

            if (hit == 0) g_dragMode = DragMode::ResizeCorner0;
            else if (hit == 1) g_dragMode = DragMode::ResizeCorner1;
            else if (hit == 2) g_dragMode = DragMode::ResizeCorner2;
            else if (hit == 3) g_dragMode = DragMode::ResizeCorner3;
            else g_dragMode = DragMode::Move;
            return 0;
        }
    }
    if (msg == WM_MOUSEMOVE && g_dragMode != DragMode::None) {
        int mx = GET_X_LPARAM(lParam);
        int my = GET_Y_LPARAM(lParam);

        if (g_dragMode == DragMode::Move) {
            double dx = (mx - g_dragStartMouse.x) / g_previewScale;
            double dy = (my - g_dragStartMouse.y) / g_previewScale;
            double ncx = g_dragOrigCX + dx;
            double ncy = g_dragOrigCY + dy;

            g_isUpdatingUI = true;
            SetWindowTextA(hCX, FormatDouble(ncx).c_str());
            SetWindowTextA(hCY, FormatDouble(ncy).c_str());
            g_isUpdatingUI = false;
            UpdateValues();
        } else {
            TransformConfig cfg = g_cfgStore.Load();
            double rad = (-cfg.rotation_deg) * (PI / 180.0);
            double cos_val = std::cos(rad);
            double sin_val = std::sin(rad);

            double curWorldX = (mx - g_originX) / g_previewScale - g_dragOrigCX;
            double curWorldY = (my - g_originY) / g_previewScale - g_dragOrigCY;

            double lx = curWorldX * cos_val - curWorldY * sin_val;
            double ly = curWorldX * sin_val + curWorldY * cos_val;

            double nw = std::abs(lx) * 2.0;
            double nh = std::abs(ly) * 2.0;

            bool locked = (SendMessage(chkRatio, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (locked) {
                char rBuf[32];
                GetWindowTextA(hRatio, rBuf, 32);
                double ratio = ParseAspectRatio(rBuf);
                nh = nw / ratio;
            }

            g_isUpdatingUI = true;
            SetWindowTextA(hW, FormatDouble(nw).c_str());
            SetWindowTextA(hH, FormatDouble(nh).c_str());
            g_isUpdatingUI = false;
            UpdateValues();
        }
        return 0;
    }
    if (msg == WM_LBUTTONUP && g_dragMode != DragMode::None) {
        ReleaseCapture();
        g_dragMode = DragMode::None;
        return 0;
    }

    if (msg == WM_COMMAND) {
        if (HIWORD(wParam) == CBN_SELCHANGE && reinterpret_cast<HWND>(lParam) == hCboMon) {
            int sel = static_cast<int>(SendMessage(hCboMon, CB_GETCURSEL, 0, 0));
            if (sel >= 0 && sel < static_cast<int>(g_monitors.size())) {
                g_target_monitor_idx = sel;
            } else {
                g_target_monitor_idx = -1;
            }
            UpdateValues();
            UpdateStatusText();
        }
        if (LOWORD(wParam) == 1003) {
            AutoMatchRatio();
        }
        if (LOWORD(wParam) == 1002) {
            if (SendMessage(chkRatio, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                char rBuf[32];
                GetWindowTextA(hRatio, rBuf, 32);
                double ratio = ParseAspectRatio(rBuf);
                double curW = ParseInput(hW);
                g_isUpdatingUI = true;
                SetWindowTextA(hH, FormatDouble(curW / ratio).c_str());
                g_isUpdatingUI = false;
                UpdateValues();
            }
            SaveConfig();
        }
        if (HIWORD(wParam) == EN_CHANGE && g_guiReady && !g_isUpdatingUI) {
            HWND src = reinterpret_cast<HWND>(lParam);
            bool locked = (SendMessage(chkRatio, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (locked && (src == hW || src == hH || src == hRatio)) {
                char rBuf[32];
                GetWindowTextA(hRatio, rBuf, 32);
                double ratio = ParseAspectRatio(rBuf);
                g_isUpdatingUI = true;
                if (src == hW || src == hRatio) {
                    double cw = ParseInput(hW);
                    SetWindowTextA(hH, FormatDouble(cw / ratio).c_str());
                } else if (src == hH) {
                    double ch = ParseInput(hH);
                    SetWindowTextA(hW, FormatDouble(ch * ratio).c_str());
                }
                g_isUpdatingUI = false;
            }
            UpdateValues();
        }
        if (LOWORD(wParam) == 1001) {
            ResetToFullArea();
        }
        if (LOWORD(wParam) == IDM_TRAY_OPEN) {
            RestoreWindow(hwnd);
        }
        if (LOWORD(wParam) == IDM_TRAY_EXIT) {
            ShutdownDriver();
        }
    }
    if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX) {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, COLOR_TEXT);
        SetBkColor(hdc, COLOR_EDIT_BG);
        return reinterpret_cast<LRESULT>(g_hBrushEdit);
    }
    if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORBTN) {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, COLOR_TEXT_MUTED);
        SetBkColor(hdc, COLOR_BG);
        return reinterpret_cast<LRESULT>(g_hBrushBg);
    }
    if (msg == WM_DRAWITEM) {
        auto pDIS = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
        if (pDIS->CtlID == 1001 || pDIS->CtlID == 1003) {
            FillRect(pDIS->hDC, &pDIS->rcItem, g_hBrushBg);

            bool pressed = (pDIS->itemState & ODS_SELECTED);
            HBRUSH btnBrush = CreateSolidBrush(pressed ? COLOR_BTN_HOVER : COLOR_BTN);
            HPEN btnPen = CreatePen(PS_SOLID, 1, COLOR_BORDER);
            HGDIOBJ ob = SelectObject(pDIS->hDC, btnBrush);
            HGDIOBJ op = SelectObject(pDIS->hDC, btnPen);

            RoundRect(pDIS->hDC, pDIS->rcItem.left, pDIS->rcItem.top, pDIS->rcItem.right, pDIS->rcItem.bottom, 6, 6);

            SetBkMode(pDIS->hDC, TRANSPARENT);
            SetTextColor(pDIS->hDC, COLOR_TEXT);
            SelectObject(pDIS->hDC, (pDIS->CtlID == 1003) ? g_hFontSmall : g_hFont);

            const char* label = (pDIS->CtlID == 1001) ? "Full Area (Reset)" : "Auto";
            DrawTextA(pDIS->hDC, label, -1, &pDIS->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(pDIS->hDC, ob);
            SelectObject(pDIS->hDC, op);
            DeleteObject(btnBrush);
            DeleteObject(btnPen);
            return TRUE;
        }
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBm = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ oldBm = SelectObject(memDC, memBm);

        FillRect(memDC, &rc, g_hBrushBg);
        DrawPreview(memDC);

        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBm);
        DeleteObject(memBm);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_CLOSE || msg == WM_DESTROY) {
        ShutdownDriver();
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    EnablePerMonitorDpi();
    EnableSubMillisecondTimer();
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    DisablePowerThrottling();

    g_hExitEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    g_hDeviceChangeEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);

    g_baseDir = GetExeDirectory();
    RefreshMonitors();
    DetectAndInitTablet();
    LoadConfig();

    g_hBrushBg = CreateSolidBrush(COLOR_BG);
    g_hBrushEdit = CreateSolidBrush(COLOR_EDIT_BG);
    g_hFont = CreateFontA(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    g_hFontSmall = CreateFontA(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hbrBackground = g_hBrushBg;
    wc.lpszClassName = "osuPoint_Driver";
    RegisterClassA(&wc);

    g_hwnd = CreateWindowA("osuPoint_Driver", "osu!Point",
        WS_OVERLAPPEDWINDOW ^ (WS_THICKFRAME | WS_MAXIMIZEBOX),
        CW_USEDEFAULT, CW_USEDEFAULT, 570, 310, NULL, NULL, hInst, NULL);

    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    DEV_BROADCAST_DEVICEINTERFACE_A devFilter{};
    devFilter.dbcc_size = sizeof(devFilter);
    devFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    devFilter.dbcc_classguid = hidGuid;
    g_hDevNotify = RegisterDeviceNotificationA(g_hwnd, &devFilter, DEVICE_NOTIFY_WINDOW_HANDLE);

    BOOL useDarkMode = TRUE;
    if (FAILED(DwmSetWindowAttribute(g_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode)))) {
        DwmSetWindowAttribute(g_hwnd, 19, &useDarkMode, sizeof(useDarkMode));
    }

    HICON hIcon = CreateAppIcon();
    if (hIcon) {
        SendMessage(g_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
        SendMessage(g_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));

        g_nid.cbSize = sizeof(NOTIFYICONDATAA);
        g_nid.hWnd = g_hwnd;
        g_nid.uID = 1;
        g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAYICON;
        g_nid.hIcon = hIcon;
        Shell_NotifyIconA(NIM_ADD, &g_nid);
    }

    TransformConfig initialCfg = g_cfgStore.Load();

    HWND lblMon = CreateWindowA("STATIC", "Display:", WS_VISIBLE | WS_CHILD, 15, 14, 55, 20, g_hwnd, NULL, NULL, NULL);
    hCboMon = CreateWindowA("COMBOBOX", "", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 75, 11, 140, 120, g_hwnd, NULL, NULL, NULL);

    for (size_t i = 0; i < g_monitors.size(); ++i) {
        char mDesc[64];
        int mw = g_monitors[i].rc.right - g_monitors[i].rc.left;
        int mh = g_monitors[i].rc.bottom - g_monitors[i].rc.top;
        snprintf(mDesc, sizeof(mDesc), "Screen %d (%dx%d)%s", static_cast<int>(i + 1), mw, mh, g_monitors[i].isPrimary ? " *" : "");
        SendMessageA(hCboMon, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(mDesc));
    }
    SendMessageA(hCboMon, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Entire Desktop"));

    if (g_target_monitor_idx >= 0 && g_target_monitor_idx < static_cast<int>(g_monitors.size())) {
        SendMessage(hCboMon, CB_SETCURSEL, g_target_monitor_idx, 0);
    } else {
        SendMessage(hCboMon, CB_SETCURSEL, g_monitors.size(), 0);
    }

    HWND lblW = CreateWindowA("STATIC", "Width (mm):", WS_VISIBLE | WS_CHILD, 15, 42, 105, 20, g_hwnd, NULL, NULL, NULL);
    hW = CreateWindowA("EDIT", FormatDouble(initialCfg.width_mm).c_str(), WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 125, 40, 90, 22, g_hwnd, NULL, NULL, NULL);

    HWND lblH = CreateWindowA("STATIC", "Height (mm):", WS_VISIBLE | WS_CHILD, 15, 70, 105, 20, g_hwnd, NULL, NULL, NULL);
    hH = CreateWindowA("EDIT", FormatDouble(initialCfg.height_mm).c_str(), WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 125, 68, 90, 22, g_hwnd, NULL, NULL, NULL);

    HWND lblCX = CreateWindowA("STATIC", "Center X (mm):", WS_VISIBLE | WS_CHILD, 15, 98, 105, 20, g_hwnd, NULL, NULL, NULL);
    hCX = CreateWindowA("EDIT", FormatDouble(initialCfg.cx).c_str(), WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 125, 96, 90, 22, g_hwnd, NULL, NULL, NULL);

    HWND lblCY = CreateWindowA("STATIC", "Center Y (mm):", WS_VISIBLE | WS_CHILD, 15, 126, 105, 20, g_hwnd, NULL, NULL, NULL);
    hCY = CreateWindowA("EDIT", FormatDouble(initialCfg.cy).c_str(), WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 125, 124, 90, 22, g_hwnd, NULL, NULL, NULL);

    HWND lblRot = CreateWindowA("STATIC", "Rotation (deg):", WS_VISIBLE | WS_CHILD, 15, 154, 105, 20, g_hwnd, NULL, NULL, NULL);
    hRot = CreateWindowA("EDIT", FormatDouble(initialCfg.rotation_deg).c_str(), WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 125, 152, 90, 22, g_hwnd, NULL, NULL, NULL);

    chkRatio = CreateWindowA("BUTTON", "Ratio:", WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX, 15, 182, 60, 22, g_hwnd, reinterpret_cast<HMENU>(1002), NULL, NULL);
    hRatio = CreateWindowA("EDIT", "16:9", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 80, 182, 75, 22, g_hwnd, NULL, NULL, NULL);
    btnAutoRatio = CreateWindowA("BUTTON", "Auto", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 162, 182, 53, 22, g_hwnd, reinterpret_cast<HMENU>(1003), NULL, NULL);

    HWND btnReset = CreateWindowA("BUTTON", "Full Area (Reset)", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 15, 218, 200, 26, g_hwnd, reinterpret_cast<HMENU>(1001), NULL, NULL);

    HWND uiControls[] = { lblMon, hCboMon, lblW, hW, lblH, hH, lblCX, hCX, lblCY, hCY, lblRot, hRot, chkRatio, hRatio, btnAutoRatio, btnReset };
    for (HWND c : uiControls) {
        SendMessage(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
    }
    SendMessage(btnAutoRatio, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontSmall), TRUE);

    g_guiReady = true;
    UpdateValues();
    UpdateStatusText();

    SetTimer(g_hwnd, 1, 200, NULL);

    ShowWindow(g_hwnd, nCmdShow);

    g_driverThread = std::thread(DriverThread);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    ShutdownDriver();

    if (g_hBrushBg) DeleteObject(g_hBrushBg);
    if (g_hBrushEdit) DeleteObject(g_hBrushEdit);
    if (g_hFont) DeleteObject(g_hFont);
    if (g_hFontSmall) DeleteObject(g_hFontSmall);

    return 0;
}
