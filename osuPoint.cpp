#ifndef _HAS_EXCEPTIONS
#define _HAS_EXCEPTIONS 0
#endif

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
#include <cstdint>
#include <fstream>
#include <sstream>
#include <tlhelp32.h>

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
constexpr UINT IDM_TRAY_PENCLICK = 2003;

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
} THREAD_POWER_THROTTLING_STATE, * PTHREAD_POWER_THROTTLING_STATE;
#define THREAD_POWER_THROTTLING_CURRENT_VERSION 1
#define THREAD_POWER_THROTTLING_EXECUTION_SPEED 1
#endif

#ifndef PROCESS_POWER_THROTTLING_CURRENT_VERSION
typedef struct _PROCESS_POWER_THROTTLING_STATE {
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
} PROCESS_POWER_THROTTLING_STATE, * PPROCESS_POWER_THROTTLING_STATE;
#define PROCESS_POWER_THROTTLING_CURRENT_VERSION 1
#define PROCESS_POWER_THROTTLING_EXECUTION_SPEED 1
#endif

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// UI theme colors
const COLORREF COLOR_BG = RGB(18, 18, 22);
const COLORREF COLOR_CARD = RGB(26, 26, 32);
const COLORREF COLOR_EDIT_BG = RGB(32, 32, 40);
const COLORREF COLOR_TEXT = RGB(240, 240, 245);
const COLORREF COLOR_TEXT_MUTED = RGB(145, 145, 165);
const COLORREF COLOR_ACCENT = RGB(255, 102, 170);
const COLORREF COLOR_BTN = RGB(34, 30, 42);
const COLORREF COLOR_BTN_HOVER = RGB(50, 40, 60);
const COLORREF COLOR_BORDER = RGB(58, 58, 72);
const COLORREF COLOR_BORDER_ACC = RGB(85, 60, 95);

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
    bool is_mouse_mode = false; // Set to true when connected via absolute mouse HID interface
    std::vector<BYTE> init_feature{ 0x02, 0x02 };
    std::vector<BYTE> init_output{};
};

#include "builtin_tablets.h"

namespace VendorID {
    constexpr USHORT Wacom = 0x056A;
    constexpr USHORT XPPen = 0x28BD;
    constexpr USHORT UCLogic = 0x5543;
    constexpr USHORT Huion = 0x256C;
    constexpr USHORT Veikk1 = 0x2FEB;
    constexpr USHORT Veikk2 = 0x2F88;
    constexpr USHORT Artisul = 0x2610;
    constexpr USHORT Winbest = 0x0416;
    constexpr USHORT Ugee = 0x2724;

    constexpr bool IsWacom(USHORT vid) noexcept {
        return vid == Wacom;
    }
    constexpr bool IsXPPenFamily(USHORT vid) noexcept {
        return vid == XPPen || vid == UCLogic || vid == Ugee;
    }
    constexpr bool IsHuionFamily(USHORT vid) noexcept {
        return vid == Huion;
    }
    constexpr bool IsVeikk(USHORT vid) noexcept {
        return vid == Veikk1 || vid == Veikk2;
    }
    constexpr bool IsKnownTabletVendor(USHORT vid) noexcept {
        return vid == XPPen || vid == UCLogic || vid == Huion ||
            vid == Veikk1 || vid == Veikk2 || vid == Wacom ||
            vid == Artisul || vid == Winbest || vid == Ugee;
    }
}

inline void ParseHexBytes(const char* hexStr, std::vector<BYTE>& outBytes) {
    outBytes.clear();
    if (!hexStr) return;
    const char* p = hexStr;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') ++p;
        if (!*p) break;
        char* endP = nullptr;
        unsigned long b = std::strtoul(p, &endP, 0);
        if (p == endP) break;
        outBytes.push_back(static_cast<BYTE>(b));
        p = endP;
    }
}

inline void ApplyBuiltinSpec(const BuiltinTabletDef& def, TabletSpec& spec) {
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
    ParseHexBytes(def.init_feature, spec.init_feature);
    ParseHexBytes(def.init_output, spec.init_output);
}

struct DisplayMonitor {
    int index = 0;
    std::string name;
    RECT rc{};
    bool isPrimary = false;
};

// 64-byte aligned transformation matrix optimized for SIMD operations
struct alignas(64) TransformConfig {
    alignas(16) double kx[2] = { 0.0, 0.0 }; // k00, k10 (scale factors for raw_x)
    alignas(16) double ky[2] = { 0.0, 0.0 }; // k01, k11 (scale factors for raw_y)
    alignas(16) double kc[2] = { 0.0, 0.0 }; // Constant offsets + 0.5 rounding

    alignas(16) double knx[2] = { 0.0, 0.0 }; // Normalized scale for raw_x -> [0, 1]
    alignas(16) double kny[2] = { 0.0, 0.0 }; // Normalized scale for raw_y -> [0, 1]
    alignas(16) double knc[2] = { 0.0, 0.0 }; // Normalized constant offset

    alignas(16) double vscale[2] = { 65535.0, 65535.0 }; // Screen scale vector [X, Y]
    alignas(16) double vshift[2] = { 0.5, 0.5 };         // Screen shift vector [X, Y]

    double width_mm = 152.0;
    double height_mm = 95.0;
    double cx = 76.0;
    double cy = 47.5;
    double rotation_deg = 0.0;
    int monitor_idx = 0;

    bool clip_area = false;
};

// Lock-free configuration storage with seqlock synchronization
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

    [[nodiscard]] inline uint32_t GetSequence() const noexcept {
        return m_seq.load(std::memory_order_relaxed);
    }

private:
    std::atomic<uint32_t> m_seq{ 0 };
    TransformConfig m_cfg{};
};

TabletSpec g_spec;
AtomicConfigStore g_cfgStore;

// Single-slot report buffer updated directly by the reader thread
struct alignas(64) TabletReport {
    int32_t raw_x = 0;
    int32_t raw_y = 0;
    uint64_t hw_timestamp_qpc = 0; // Timestamp recorded at packet decode time
    uint32_t frame_id = 0;         // Monotonic frame counter (0 = uninitialized)
    bool pressed = false;          // Pen tip contact state
};

// Lock-free single-slot queue: writer updates seq odd -> even around write operations
class alignas(64) AtomicReportStore {
public:
    __forceinline void Store(const TabletReport& r) noexcept {
        uint32_t s = m_seq.load(std::memory_order_relaxed);
        m_seq.store(s + 1, std::memory_order_release);
        m_report = r;
        m_seq.store(s + 2, std::memory_order_release);
    }

    __forceinline TabletReport Load() noexcept {
        TabletReport out;
        uint32_t s1 = 0, s2 = 0;
        do {
            s1 = m_seq.load(std::memory_order_acquire);
            while (s1 & 1) {
                _mm_pause();
                s1 = m_seq.load(std::memory_order_acquire);
            }
            out = m_report;
            s2 = m_seq.load(std::memory_order_acquire);
        } while (s1 != s2);
        return out;
    }

    // Fast path: peek frame_id without copying the entire struct
    [[nodiscard]] __forceinline uint32_t PeekFrameId() const noexcept {
        uint32_t s = m_seq.load(std::memory_order_acquire);
        if (s & 1) return 0; // Write currently in progress
        uint32_t fid = m_report.frame_id;
        if (m_seq.load(std::memory_order_acquire) != s) return 0;
        return fid;
    }

private:
    std::atomic<uint32_t> m_seq{ 0 };
    TabletReport m_report{};
};

AtomicReportStore g_reportStore;

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

std::atomic<bool> g_exitDriver{ false };
HANDLE g_hDeviceChangeEvent = NULL;
HANDLE g_reportReadyEvent = NULL;
HDEVNOTIFY g_hDevNotify = NULL;

std::thread g_readerThread;
std::thread g_processingThread;

LARGE_INTEGER g_qpcFreq{ 0 };
double g_qpcToUs = 0.0;

std::atomic<uint64_t> g_reportsReceived{ 0 };
std::atomic<uint64_t> g_rawReportsReceived{ 0 };
std::atomic<uint64_t> g_reportsActedOn{ 0 };
std::atomic<uint64_t> g_reportsDeduped{ 0 };
std::atomic<uint32_t> g_reconnectCount{ 0 };
std::atomic<uint32_t> g_errorCount{ 0 };
std::atomic<DWORD>    g_lastReadError{ 0 };
std::atomic<int>      g_lastReportLen{ 0 };
std::atomic<uint32_t> g_lastReportBytes{ 0 };
std::atomic<uint64_t> g_lastReportQpc{ 0 };
std::atomic<int>      g_numActiveEndpoints{ 0 };

inline void LogMessage([[maybe_unused]] const std::string& msg) noexcept {}

HFONT g_hFont = NULL;
HFONT g_hFontSmall = NULL;
HBRUSH g_hBrushBg = NULL;
HBRUSH g_hBrushEdit = NULL;
NOTIFYICONDATAA g_nid = { 0 };

HWND hCboMon = NULL;
WNDPROC g_origCboProc = NULL;
HWND hW = NULL, hH = NULL, hCX = NULL, hCY = NULL, hRot = NULL;
HWND chkRatio = NULL, hRatio = NULL, btnAutoRatio = NULL;
HWND chkLockDrag = NULL;
HWND chkClipArea = NULL;
HWND chkPenClick = NULL;
bool g_lockRatio = false;
bool g_lockDrag = false;
bool g_clipArea = false;
std::atomic<bool> g_penClick{ true };

HHOOK g_hMouseHook = NULL;

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_spec.is_mouse_mode && !g_penClick.load(std::memory_order_relaxed)) {
        if (wParam == WM_LBUTTONDOWN || wParam == WM_LBUTTONUP || wParam == WM_NCLBUTTONDOWN || wParam == WM_NCLBUTTONUP) {
            auto* pMouse = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
            // Block physical clicks generated by the tablet in mouse mode when pen click is disabled
            if ((pMouse->flags & LLMHF_INJECTED) == 0) {
                return 1;
            }
        }
    }
    return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
}

// Tablet preview canvas metrics
constexpr int PREVIEW_X = 235;
constexpr int PREVIEW_Y = 16;
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

// Scans for active tablet drivers that might conflict with exclusive device access
inline std::string GetConflictingDriverProcess() {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return "";
    PROCESSENTRY32W pe{ sizeof(PROCESSENTRY32W) };
    if (Process32FirstW(hSnap, &pe)) {
        do {
            char mbName[MAX_PATH] = {};
            WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, mbName, sizeof(mbName), nullptr, nullptr);
            std::string name = ToLower(mbName);
            if (name.find("opentabletdriver") != std::string::npos ||
                name.find("pentablet") != std::string::npos ||
                name.find("tabletdriver") != std::string::npos ||
                name.find("vmulti") != std::string::npos) {
                CloseHandle(hSnap);
                return mbName;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return "";
}

void PreciseSleep(DWORD ms) noexcept {
    static HANDLE hTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (hTimer) {
        LARGE_INTEGER due;
        due.QuadPart = -static_cast<LONGLONG>(ms) * 10000LL;
        SetWaitableTimer(hTimer, &due, 0, nullptr, nullptr, FALSE);
        WaitForSingleObject(hTimer, INFINITE);
    }
    else {
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

// Maps reader and processor threads to isolated P-cores to eliminate thread contention and jitter
struct AffinityPair {
    DWORD_PTR reader = 0;
    DWORD_PTR processor = 0;
    DWORD_PTR ui = 0;       // UI/WinMain thread mask (core 0 or lowest available)
    DWORD   readerCore = 0; // Logical processor index
    DWORD   processorCore = 0;
};

AffinityPair CalculateDualAffinity() noexcept {
    AffinityPair result;

    DWORD_PTR processAffinity = 0, systemAffinity = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &processAffinity, &systemAffinity) || processAffinity == 0) {
        return result;
    }

    DWORD returnLength = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &returnLength);
    if (returnLength == 0) return result;

    std::vector<BYTE> buffer(returnLength);
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore,
        reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &returnLength)) {
        return result;
    }

    // Determine highest efficiency class (performance cores on hybrid architectures)
    BYTE maxEfficiency = 0;
    {
        BYTE* ptr = buffer.data();
        BYTE* end = ptr + returnLength;
        while (ptr < end) {
            auto cur = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(ptr);
            if (cur->Relationship == RelationProcessorCore) {
                if (cur->Processor.EfficiencyClass > maxEfficiency)
                    maxEfficiency = cur->Processor.EfficiencyClass;
            }
            ptr += cur->Size;
        }
    }

    // Collect P-core masks, prioritizing non-zero cores to avoid OS interrupt handling
    DWORD_PTR pCoreMasks[64] = {};
    int pCoreCount = 0;
    bool core0Found = false;
    DWORD_PTR core0Mask = 0;

    {
        BYTE* ptr = buffer.data();
        BYTE* end = ptr + returnLength;
        while (ptr < end && pCoreCount < 64) {
            auto cur = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(ptr);
            if (cur->Relationship == RelationProcessorCore && cur->Processor.GroupCount > 0) {
                DWORD_PTR mask = cur->Processor.GroupMask[0].Mask & processAffinity;
                if (mask != 0) {
                    if (!core0Found) { core0Mask = mask; core0Found = true; }
                    if (cur->Processor.EfficiencyClass == maxEfficiency) {
                        pCoreMasks[pCoreCount++] = mask;
                    }
                }
            }
            ptr += cur->Size;
        }
    }

    if (pCoreCount >= 2) {
        int picked = 0;
        DWORD_PTR chosen[2] = {};
        for (int i = 0; i < pCoreCount && picked < 2; ++i) {
            if (pCoreMasks[i] != core0Mask) chosen[picked++] = pCoreMasks[i];
        }
        if (picked == 2) {
            unsigned long idx0 = 0, idx1 = 0;
#if defined(_M_X64) || defined(__x86_64__)
            _BitScanForward64(&idx0, chosen[0]); _BitScanForward64(&idx1, chosen[1]);
#else
            _BitScanForward(&idx0, static_cast<unsigned long>(chosen[0]));
            _BitScanForward(&idx1, static_cast<unsigned long>(chosen[1]));
#endif
            result.reader = (1ULL << idx0);
            result.readerCore = static_cast<DWORD>(idx0);
            result.processor = (1ULL << idx1);
            result.processorCore = static_cast<DWORD>(idx1);
            if (core0Mask) {
                unsigned long idxUI = 0;
#if defined(_M_X64) || defined(__x86_64__)
                _BitScanForward64(&idxUI, core0Mask);
#else
                _BitScanForward(&idxUI, static_cast<unsigned long>(core0Mask));
#endif
                result.ui = (1ULL << idxUI);
            }
            else {
                result.ui = result.reader;
            }
            return result;
        }
        if (picked == 1) {
            unsigned long idx0 = 0;
#if defined(_M_X64) || defined(__x86_64__)
            _BitScanForward64(&idx0, chosen[0]);
#else
            _BitScanForward(&idx0, static_cast<unsigned long>(chosen[0]));
#endif
            result.reader = (1ULL << idx0);
            result.readerCore = static_cast<DWORD>(idx0);
            if (core0Mask) {
                unsigned long idxC = 0;
#if defined(_M_X64) || defined(__x86_64__)
                _BitScanForward64(&idxC, core0Mask);
#else
                _BitScanForward(&idxC, static_cast<unsigned long>(core0Mask));
#endif
                result.processor = (1ULL << idxC);
                result.processorCore = static_cast<DWORD>(idxC);
                result.ui = (1ULL << idxC);
            }
            else {
                result.processor = result.reader;
                result.processorCore = result.readerCore;
                result.ui = result.reader;
            }
            return result;
        }
    }

    // Fallback: pick available logical processors sequentially
    DWORD_PTR remaining = processAffinity;
    unsigned long idxA = 0, idxB = 0;
    bool gotA = false, gotB = false;
#if defined(_M_X64) || defined(__x86_64__)
    if (_BitScanForward64(&idxA, remaining)) { gotA = true; remaining &= ~(1ULL << idxA); }
    if (gotA && _BitScanForward64(&idxB, remaining)) gotB = true;
#else
    if (_BitScanForward(&idxA, static_cast<unsigned long>(remaining))) { gotA = true; remaining &= ~(1UL << idxA); }
    if (gotA && _BitScanForward(&idxB, static_cast<unsigned long>(remaining))) gotB = true;
#endif
    result.reader = gotA ? (1ULL << idxA) : processAffinity;
    result.readerCore = gotA ? static_cast<DWORD>(idxA) : 0;
    result.processor = gotB ? (1ULL << idxB) : result.reader;
    result.processorCore = gotB ? static_cast<DWORD>(idxB) : result.readerCore;
    result.ui = result.reader;
    return result;
}

AffinityPair g_affinityPair;

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
    spec.init_output.clear();

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
        else if (key == "init_output") {
            const char* p = val.c_str();
            while (*p) {
                while (*p == ' ' || *p == '\t' || *p == ',') ++p;
                if (!*p) break;
                char* endP = nullptr;
                unsigned long b = std::strtoul(p, &endP, 0);
                if (p == endP) break;
                spec.init_output.push_back(static_cast<BYTE>(b));
                p = endP;
            }
        }
    }
    return true;
}

std::string GetDevicePath(USHORT vid, USHORT pid, int targetReportLen) {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO devInfo = SetupDiGetClassDevsA(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return "";

    SP_DEVICE_INTERFACE_DATA devData{ sizeof(SP_DEVICE_INTERFACE_DATA) };
    std::string digitizerPath = "";
    std::string reportLenMatchedPath = "";
    std::string vendorMatchedPath = "";
    std::string mousePath = "";
    std::string fallbackPath = "";

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
                    if (attr.VendorID == vid && (pid == 0 || attr.ProductID == pid)) {
                        PHIDP_PREPARSED_DATA ppData = nullptr;
                        if (HidD_GetPreparsedData(h, &ppData)) {
                            HIDP_CAPS caps{};
                            if (HidP_GetCaps(ppData, &caps) == HIDP_STATUS_SUCCESS) {
                                // Priority 1: Digitizer interface (Pen / Digitizer usage)
                                if (caps.UsagePage == 0x0D || caps.UsagePage == 0xFF0D) {
                                    if (targetReportLen <= 0 || caps.InputReportByteLength == targetReportLen || caps.InputReportByteLength == targetReportLen + 1 || caps.InputReportByteLength == targetReportLen + 2) {
                                        digitizerPath = detail->DevicePath;
                                        HidD_FreePreparsedData(ppData);
                                        CloseHandle(h);
                                        free(detail);
                                        break;
                                    }
                                    if (digitizerPath.empty()) {
                                        digitizerPath = detail->DevicePath;
                                    }
                                }
                                // Priority 2: Report byte length match
                                else if (targetReportLen > 0 && (caps.InputReportByteLength == targetReportLen || caps.InputReportByteLength == targetReportLen + 1 || caps.InputReportByteLength == targetReportLen + 2)) {
                                    reportLenMatchedPath = detail->DevicePath;
                                    HidD_FreePreparsedData(ppData);
                                    CloseHandle(h);
                                    free(detail);
                                    break;
                                }
                                // Priority 3: Vendor-specific raw endpoint
                                else if (caps.UsagePage >= 0xFF00) {
                                    if (vendorMatchedPath.empty()) {
                                        vendorMatchedPath = detail->DevicePath;
                                    }
                                }
                                // Priority 4: Generic desktop mouse / pointer fallback
                                else if (caps.UsagePage == 0x01 && (caps.Usage == 0x01 || caps.Usage == 0x02)) {
                                    if (mousePath.empty()) {
                                        mousePath = detail->DevicePath;
                                    }
                                }
                            }
                            HidD_FreePreparsedData(ppData);
                        }
                        if (fallbackPath.empty() && strstr(detail->DevicePath, "&mi_00") != nullptr) {
                            fallbackPath = detail->DevicePath;
                        }
                        else if (fallbackPath.empty()) {
                            fallbackPath = detail->DevicePath;
                        }
                    }
                }
                CloseHandle(h);
            }
        }
        free(detail);
    }
    SetupDiDestroyDeviceInfoList(devInfo);

    if (!digitizerPath.empty()) return digitizerPath;
    if (!reportLenMatchedPath.empty()) return reportLenMatchedPath;
    if (!vendorMatchedPath.empty()) return vendorMatchedPath;
    if (!mousePath.empty()) return mousePath;
    return fallbackPath;
}

void SetTransform(double w, double h, double cx, double cy, double rot_deg, int monitor_idx) noexcept {
    TransformConfig cfg;
    cfg.width_mm = w;
    cfg.height_mm = h;
    cfg.cx = cx;
    cfg.cy = cy;
    cfg.rotation_deg = rot_deg;
    cfg.monitor_idx = monitor_idx;
    cfg.clip_area = g_clipArea;

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

    // Direct mapping coefficients: raw -> virtual screen
    cfg.kx[0] = (inv_scale_x * a) * scale_x;
    cfg.ky[0] = (inv_scale_y * b) * scale_x;
    cfg.kc[0] = (0.5 - (cx * a + cy * b)) * scale_x + shift_x + 0.5;

    cfg.kx[1] = (inv_scale_x * c) * scale_y;
    cfg.ky[1] = (inv_scale_y * d) * scale_y;
    cfg.kc[1] = (0.5 - (cx * c + cy * d)) * scale_y + shift_y + 0.5;

    // Normalized mapping coefficients: raw -> [0, 1]
    cfg.knx[0] = inv_scale_x * a;
    cfg.kny[0] = inv_scale_y * b;
    cfg.knc[0] = 0.5 - (cx * a + cy * b);

    cfg.knx[1] = inv_scale_x * c;
    cfg.kny[1] = inv_scale_y * d;
    cfg.knc[1] = 0.5 - (cx * c + cy * d);

    // Screen scale and shift vectors
    cfg.vscale[0] = scale_x;
    cfg.vscale[1] = scale_y;
    cfg.vshift[0] = shift_x + 0.5;
    cfg.vshift[1] = shift_y + 0.5;

    g_cfgStore.Store(cfg);
}

std::vector<std::string> DetectAndInitTablet() {
    std::vector<std::string> resultPaths;
    std::string tabletsDir = g_baseDir + "tablets";

    // 1. Check custom user profiles in tablets/*.cfg
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
                    resultPaths.push_back(devPath);
                    return resultPaths;
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }

    // 2. Match against built-in tablet database
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO devInfo = SetupDiGetClassDevsA(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return resultPaths;

    int         bestScore = 0;
    std::string bestPath;
    size_t      bestBuiltinIdx = (size_t)-1;
    int         bestNameScore = -1;
    USHORT      bestVid = 0;
    USHORT      bestPid = 0;

    auto nameMatchScore = [](const char* hidProd, const char* dbName) noexcept -> int {
        if (!hidProd || !dbName) return 0;

        auto isAlNum = [](char c) noexcept -> bool {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
            };
        auto toLow = [](char c) noexcept -> char {
            return (c >= 'A' && c <= 'Z') ? char(c + 32) : c;
            };

        char h[128] = {}, n[128] = {};
        for (int i = 0; hidProd[i] && i < 127; ++i) h[i] = toLow(hidProd[i]);
        for (int i = 0; dbName[i] && i < 127; ++i) n[i] = toLow(dbName[i]);

        int score = 0;
        char tok[64] = {};
        int  ti = 0;
        for (int i = 0; ; ++i) {
            bool delim = (n[i] == ' ' || n[i] == '-' || n[i] == '_' || n[i] == '\0');
            if (!delim && ti < 63) { tok[ti++] = n[i]; continue; }
            if (ti > 0) {
                tok[ti] = '\0';
                int tokLen = ti;
                for (int j = 0; h[j]; ++j) {
                    if (strncmp(&h[j], tok, tokLen) == 0) {
                        bool leftOk = (j == 0 || !isAlNum(h[j - 1]));
                        bool rightOk = !isAlNum(h[j + tokLen]);
                        if (leftOk && rightOk) {
                            score += 10 * tokLen;
                            break;
                        }
                    }
                }
                ti = 0;
            }
            if (n[i] == '\0') break;
        }
        return score;
        };

    SP_DEVICE_INTERFACE_DATA devData{ sizeof(SP_DEVICE_INTERFACE_DATA) };
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
                    char hidProdStr[128] = {};
                    {
                        wchar_t wProd[128] = {};
                        if (HidD_GetProductString(h, wProd, sizeof(wProd)))
                            WideCharToMultiByte(CP_UTF8, 0, wProd, -1, hidProdStr, sizeof(hidProdStr), nullptr, nullptr);
                    }

                    size_t matchIdx = (size_t)-1;
                    int    nameScore = -1;
                    for (size_t b = 0; b < s_builtinTabletsCount; ++b) {
                        if (s_builtinTablets[b].vid != attr.VendorID || s_builtinTablets[b].pid != attr.ProductID)
                            continue;
                        int ns = nameMatchScore(hidProdStr, s_builtinTablets[b].name);
                        if (matchIdx == (size_t)-1 || ns > nameScore) {
                            matchIdx = b;
                            nameScore = ns;
                        }
                    }

                    if (matchIdx != (size_t)-1) {
                        int  score = 0;
                        PHIDP_PREPARSED_DATA ppData = nullptr;
                        if (HidD_GetPreparsedData(h, &ppData)) {
                            HIDP_CAPS caps{};
                            if (HidP_GetCaps(ppData, &caps) == HIDP_STATUS_SUCCESS) {
                                bool isKb = (caps.UsagePage == 0x01 && caps.Usage == 0x06);
                                bool isConsumer = (caps.UsagePage == 0x0C);
                                if (!isKb && !isConsumer) {
                                    int expectedLen = s_builtinTablets[matchIdx].report_len;
                                    if (expectedLen > 0 &&
                                        (caps.InputReportByteLength == expectedLen ||
                                            caps.InputReportByteLength == expectedLen + 1 ||
                                            caps.InputReportByteLength == expectedLen + 2)) {
                                        score += 20;
                                    }

                                    if (caps.UsagePage == 0x0D || caps.UsagePage == 0xFF0D) {
                                        score += 15;
                                    }
                                    else if (caps.UsagePage >= 0xFF00) {
                                        score += 10;
                                    }
                                    else if (caps.UsagePage == 0x01 &&
                                        (caps.Usage == 0x01 || caps.Usage == 0x02 || caps.Usage == 0x04)) {
                                        score += 1;
                                    }
                                }
                            }
                            HidD_FreePreparsedData(ppData);
                        }
                        else {
                            bool isMi00 = (strstr(detail->DevicePath, "&mi_00") != nullptr) ||
                                (strstr(detail->DevicePath, "&MI_00") != nullptr);
                            score = isMi00 ? 15 : 1;
                        }

                        if (strstr(detail->DevicePath, "&mi_00") != nullptr ||
                            strstr(detail->DevicePath, "&MI_00") != nullptr) {
                            score += 2;
                        }

                        bool isSameDevice = (bestBuiltinIdx != (size_t)-1 &&
                            bestVid == attr.VendorID && bestPid == attr.ProductID);
                        bool isNewDevice = (bestBuiltinIdx == (size_t)-1);

                        if (isNewDevice || isSameDevice) {
                            if (nameScore > bestNameScore || isNewDevice) {
                                bestBuiltinIdx = matchIdx;
                                bestNameScore = nameScore;
                                bestVid = attr.VendorID;
                                bestPid = attr.ProductID;
                            }

                            if (score > bestScore || isNewDevice) {
                                bestScore = score;
                                bestPath = detail->DevicePath;
                                if (bestBuiltinIdx == (size_t)-1) {
                                    bestBuiltinIdx = matchIdx;
                                    bestNameScore = nameScore;
                                    bestVid = attr.VendorID;
                                    bestPid = attr.ProductID;
                                }
                            }
                        }
                    }
                }
                CloseHandle(h);
            }
        }
        free(detail);
    }

    if (bestBuiltinIdx != (size_t)-1 && !bestPath.empty()) {
        ApplyBuiltinSpec(s_builtinTablets[bestBuiltinIdx], g_spec);
        resultPaths.push_back(bestPath);

        // Collect auxiliary interfaces (vendor endpoints, express keys)
        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &devData); ++i) {
            DWORD reqSize = 0;
            SetupDiGetDeviceInterfaceDetailA(devInfo, &devData, nullptr, 0, &reqSize, nullptr);
            auto detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_A>(malloc(reqSize));
            if (!detail) continue;
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
            if (SetupDiGetDeviceInterfaceDetailA(devInfo, &devData, detail, reqSize, nullptr, nullptr)) {
                if (_stricmp(detail->DevicePath, bestPath.c_str()) != 0) {
                    HANDLE h = CreateFileA(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
                    if (h != INVALID_HANDLE_VALUE) {
                        HIDD_ATTRIBUTES attr{ sizeof(HIDD_ATTRIBUTES) };
                        if (HidD_GetAttributes(h, &attr) && attr.VendorID == bestVid && attr.ProductID == bestPid) {
                            PHIDP_PREPARSED_DATA ppData = nullptr;
                            bool isKb = false;
                            if (HidD_GetPreparsedData(h, &ppData)) {
                                HIDP_CAPS caps{};
                                if (HidP_GetCaps(ppData, &caps) == HIDP_STATUS_SUCCESS) {
                                    isKb = (caps.UsagePage == 0x01 && caps.Usage == 0x06);
                                }
                                HidD_FreePreparsedData(ppData);
                            }
                            if (!isKb) {
                                resultPaths.push_back(detail->DevicePath);
                            }
                        }
                        CloseHandle(h);
                    }
                }
            }
            free(detail);
        }

        SetupDiDestroyDeviceInfoList(devInfo);
        return resultPaths;
    }

    // 3. Fallback enumeration for unlisted devices
    auto isKnownVendor = [](USHORT vid) noexcept -> bool {
        return VendorID::IsKnownTabletVendor(vid);
        };

    auto fillAutoSpec = [](TabletSpec& spec, USHORT vid, USHORT pid, USHORT reportLen, HANDLE h) noexcept -> void {
        spec.vid = vid; spec.pid = pid; spec.report_len = reportLen;
        wchar_t prodStr[128] = {};
        if (HidD_GetProductString(h, prodStr, sizeof(prodStr))) {
            char mbProd[128] = {};
            WideCharToMultiByte(CP_UTF8, 0, prodStr, -1, mbProd, sizeof(mbProd), nullptr, nullptr);
            spec.name = mbProd;
        }
        else {
            char hexName[64];
            snprintf(hexName, sizeof(hexName), "HID Tablet [%04X:%04X]", vid, pid);
            spec.name = hexName;
        }
        if (VendorID::IsHuionFamily(vid)) {
            spec.report_id = 0x08; spec.max_x = 32000; spec.max_y = 20000;
            spec.phys_w = 160.0; spec.phys_h = 100.0; spec.x_offset = 2; spec.y_offset = 4;
        }
        else if (VendorID::IsXPPenFamily(vid)) {
            spec.report_id = 0x02; spec.max_x = 32000; spec.max_y = 20000;
            spec.phys_w = 160.0; spec.phys_h = 100.0; spec.x_offset = 2; spec.y_offset = 4;
        }
        else if (VendorID::IsVeikk(vid)) {
            spec.report_id = 0x02; spec.max_x = 32767; spec.max_y = 32767;
            spec.phys_w = 152.4; spec.phys_h = 101.6; spec.x_offset = 3; spec.y_offset = 5;
        }
        };

    // Pass A: scan for generic digitizers or known vendor endpoints
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
                    PHIDP_PREPARSED_DATA ppData = nullptr;
                    if (HidD_GetPreparsedData(h, &ppData)) {
                        HIDP_CAPS caps{};
                        if (HidP_GetCaps(ppData, &caps) == HIDP_STATUS_SUCCESS) {
                            bool isDigitizer = (caps.UsagePage == 0x0D || caps.UsagePage == 0xFF0D);
                            bool isVendorPipe = (caps.UsagePage >= 0xFF00 && isKnownVendor(attr.VendorID));
                            if (isDigitizer || isVendorPipe) {
                                TabletSpec autoSpec{};
                                fillAutoSpec(autoSpec, attr.VendorID, attr.ProductID, caps.InputReportByteLength, h);
                                g_spec = autoSpec;
                                std::string foundPath = detail->DevicePath;
                                HidD_FreePreparsedData(ppData);
                                CloseHandle(h);
                                free(detail);
                                SetupDiDestroyDeviceInfoList(devInfo);
                                resultPaths.push_back(foundPath);
                                return resultPaths;
                            }
                        }
                        HidD_FreePreparsedData(ppData);
                    }
                }
                CloseHandle(h);
            }
        }
        free(detail);
    }

    // Pass B: scan for absolute mouse fallback interfaces
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
                    if (isKnownVendor(attr.VendorID)) {
                        PHIDP_PREPARSED_DATA ppData = nullptr;
                        if (HidD_GetPreparsedData(h, &ppData)) {
                            HIDP_CAPS caps{};
                            if (HidP_GetCaps(ppData, &caps) == HIDP_STATUS_SUCCESS) {
                                bool isMouse = (caps.UsagePage == 0x01 &&
                                    (caps.Usage == 0x01 || caps.Usage == 0x02 || caps.Usage == 0x04));
                                if (isMouse) {
                                    TabletSpec autoSpec{};
                                    fillAutoSpec(autoSpec, attr.VendorID, attr.ProductID, caps.InputReportByteLength, h);
                                    autoSpec.is_mouse_mode = true;
                                    g_spec = autoSpec;
                                    std::string foundPath = detail->DevicePath;
                                    HidD_FreePreparsedData(ppData);
                                    CloseHandle(h);
                                    free(detail);
                                    SetupDiDestroyDeviceInfoList(devInfo);
                                    resultPaths.push_back(foundPath);
                                    return resultPaths;
                                }
                            }
                            HidD_FreePreparsedData(ppData);
                        }
                    }
                }
                CloseHandle(h);
            }
        }
        free(detail);
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return resultPaths;
}

HANDLE OpenTabletHandle(const std::string& devPath) noexcept {
    HANDLE hDevice = CreateFileA(devPath.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);

    if (hDevice == INVALID_HANDLE_VALUE) {
        hDevice = CreateFileA(devPath.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    }
    if (hDevice == INVALID_HANDLE_VALUE) {
        hDevice = CreateFileA(devPath.c_str(), GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    }
    if (hDevice == INVALID_HANDLE_VALUE) {
        hDevice = CreateFileA(devPath.c_str(), GENERIC_READ,
            0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
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
    f << "lock_drag=" << (g_lockDrag ? 1 : 0) << "\n";
    f << "clip_area=" << (g_clipArea ? 1 : 0) << "\n";
    f << "pen_click=" << (g_penClick.load(std::memory_order_relaxed) ? 1 : 0) << "\n";
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

    static std::string s_conflict = "";
    static int s_conflictTicks = 0;
    if (++s_conflictTicks >= 4) {
        s_conflictTicks = 0;
        s_conflict = GetConflictingDriverProcess();
    }

    char title[256];
    char tip[256];

    if (!s_conflict.empty()) {
        snprintf(title, sizeof(title), "osu!Point - [CLOSE %s IN TASK MANAGER!]", s_conflict.c_str());
        snprintf(tip, sizeof(tip), "Close %s in Task Manager to allow osu!Point access!", s_conflict.c_str());
    }
    else if (g_hDevice != INVALID_HANDLE_VALUE) {
        uint64_t reports = g_reportsReceived.load(std::memory_order_relaxed);
        uint64_t raw = g_rawReportsReceived.load(std::memory_order_relaxed);
        int eps = g_numActiveEndpoints.load(std::memory_order_relaxed);
        DWORD lastErr = g_lastReadError.load(std::memory_order_relaxed);
        snprintf(title, sizeof(title), "osu!Point - [%s]", g_spec.name.c_str());

        uint32_t b4 = g_lastReportBytes.load(std::memory_order_relaxed);
        BYTE b0 = b4 & 0xFF, b1 = (b4 >> 8) & 0xFF, b2 = (b4 >> 16) & 0xFF, b3 = (b4 >> 24) & 0xFF;
        snprintf(tip, sizeof(tip), "%s | P: %llu, Raw: %llu, EPs: %d, Err: %lu, B: %02X %02X %02X %02X",
            g_spec.name.c_str(), reports, raw, eps, lastErr, b0, b1, b2, b3);
    }
    else {
        snprintf(title, sizeof(title), "osu!Point - [Waiting for tablet...]");
        snprintf(tip, sizeof(tip), "osu!Point - Waiting for tablet...");
    }

    SetWindowTextA(g_hwnd, title);
    strncpy_s(g_nid.szTip, tip, sizeof(g_nid.szTip) - 1);
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
        else if (key == "lock_ratio") { g_lockRatio = (std::atoi(valStr.c_str()) != 0); }
        else if (key == "ratio") { ratioStr = valStr; }
        else if (key == "lock_drag" || key == "lock_area") { g_lockDrag = (std::atoi(valStr.c_str()) != 0); }
        else if (key == "clip_area" || key == "clip" || key == "lock_out_of_area" || key == "ignore_outside") { g_clipArea = (std::atoi(valStr.c_str()) != 0); }
        else if (key == "pen_click" || key == "click" || key == "pen_button") { g_penClick.store(std::atoi(valStr.c_str()) != 0, std::memory_order_relaxed); }
    }

    g_target_monitor_idx = mon;

    if (!has_w || !has_h || !has_cx || !has_cy || w <= 0 || h <= 0 || cx <= 0 || cy <= 0) {
        SetDefaults();
    }
    else {
        SetTransform(w, h, cx, cy, rot, mon);
    }

    if (hRatio) SetWindowTextA(hRatio, ratioStr.c_str());
    if (chkRatio) InvalidateRect(chkRatio, NULL, FALSE);
    if (chkLockDrag) InvalidateRect(chkLockDrag, NULL, FALSE);
    if (chkClipArea) InvalidateRect(chkClipArea, NULL, FALSE);
    if (chkPenClick) InvalidateRect(chkPenClick, NULL, FALSE);

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

inline bool DecodeReport(const BYTE* report, int reportSize, const TabletSpec& spec, int32_t& out_x, int32_t& out_y, bool& out_pressed) noexcept {
    if (!report || reportSize < 4) return false;

    // Track previous report state to handle abrupt proximity exit (0xC0 sentinel)
    static thread_local bool s_prevPressed = false;
    static thread_local int32_t s_prevX = 0, s_prevY = 0;

    // XP-Pen / VEIKK: byte 0xC0 signifies the pen has left proximity
    if (VendorID::IsXPPenFamily(spec.vid) || VendorID::IsVeikk(spec.vid)) {
        bool isLift = false;
        if (reportSize > 1 && report[1] == 0xC0) isLift = true;
        if (reportSize > 2 && report[1] == 0x41 && report[2] == 0xC0) isLift = true;

        if (isLift) {
            // Emit a single release event if tip was active to prevent stuck clicks
            if (s_prevPressed) {
                s_prevPressed = false;
                out_x = s_prevX;
                out_y = s_prevY;
                out_pressed = false;
                return true;
            }
            return false;
        }
    }

    int raw_x = 0, raw_y = 0;
    bool is_pressed = false;

    // VEIKK packet format (0x41 signature)
    if (reportSize >= 7 && report[1] == 0x41 && (report[2] & 0xF0) == 0xA0) {
        raw_x = ReadLE16(&report[3]);
        raw_y = ReadLE16(&report[5]);
        is_pressed = (report[2] & 0x01) != 0;
    }
    // Huion / Gaomon (Report ID 0x08, or alternate IDs 0x07, 0x09, 0x0A, 0x0E)
    else if (VendorID::IsHuionFamily(spec.vid) && (report[0] == spec.report_id || report[0] == 0x08 || report[0] == 0x07 || report[0] == 0x09 || report[0] == 0x0A || report[0] == 0x0E)) {
        if (reportSize >= 6) {
            raw_x = ReadLE16(&report[2]);
            raw_y = ReadLE16(&report[4]);
            is_pressed = (report[1] & 0x01) != 0;
            if (reportSize >= 8) {
                is_pressed = is_pressed && (ReadLE16(&report[6]) > 0);
            }
        }
    }
    // XP-Pen (various revisions)
    else if (VendorID::IsXPPenFamily(spec.vid)) {
        if (reportSize >= 6) {
            raw_x = ReadLE16(&report[2]);
            raw_y = ReadLE16(&report[4]);
            if (reportSize >= 12) {
                raw_x |= (static_cast<int>(report[10]) << 16);
                raw_y |= (static_cast<int>(report[11]) << 16);
            }
            is_pressed = (report[1] & 0x01) != 0;
            if (reportSize >= 8) {
                is_pressed = is_pressed && (ReadLE16(&report[6]) > 0);
            }
        }
        else if (reportSize >= 5) {
            raw_x = ReadLE16(&report[1]);
            raw_y = ReadLE16(&report[3]);
            is_pressed = (report[0] & 0x01) != 0;
        }
    }
    // Standard HID / Wacom digitizer
    else {
        const int xo = spec.x_offset;
        const int yo = spec.y_offset;
        const int min_len = (std::max)(xo + 2, yo + 2);

        if (spec.report_id == 0 || report[0] == spec.report_id || report[0] == 0x10) [[likely]] {
            if (reportSize >= min_len) [[likely]] {
                raw_x = ReadLE16(&report[xo]);
                raw_y = ReadLE16(&report[yo]);
                is_pressed = (report[1] & 0x01) != 0;
            }
        }
        else {
            if (xo >= 1 && yo >= 1 && reportSize >= (std::max)(xo + 1, yo + 1)) {
                raw_x = ReadLE16(&report[xo - 1]);
                raw_y = ReadLE16(&report[yo - 1]);
                is_pressed = (report[0] & 0x01) != 0;
            }
        }
    }

    // Absolute mouse mode fallback
    if (spec.is_mouse_mode && reportSize >= 4) {
        is_pressed = (report[0] & 0x01) != 0 || (report[1] & 0x01) != 0;
    }

    // Drop empty or non-coordinate packets
    if (raw_x == 0 && raw_y == 0) {
        return false;
    }

    // Clamp coordinates to tablet limits
    if (raw_x < 0) raw_x = 0;
    if (raw_y < 0) raw_y = 0;
    if (spec.max_x > 0 && raw_x > spec.max_x) raw_x = spec.max_x;
    if (spec.max_y > 0 && raw_y > spec.max_y) raw_y = spec.max_y;

    out_x = raw_x;
    out_y = raw_y;
    out_pressed = is_pressed;

    s_prevPressed = is_pressed;
    s_prevX = raw_x;
    s_prevY = raw_y;
    return true;
}

// Dedicated HID input reading thread
void ReaderThread() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    if (g_affinityPair.reader != 0) {
        SetThreadAffinityMask(GetCurrentThread(), g_affinityPair.reader);
        SetThreadIdealProcessor(GetCurrentThread(), g_affinityPair.readerCore);
    }

    DisablePowerThrottling();
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);

    DWORD taskIndex = 0;
    HANDLE hMmcss = AvSetMmThreadCharacteristicsA("Pro Audio", &taskIndex);
    if (hMmcss) {
        AvSetMmThreadPriority(hMmcss, AVRT_PRIORITY_CRITICAL);
    }

    constexpr int MAX_ENDPOINTS = 4;
    struct EndpointSlot {
        HANDLE hDevice = INVALID_HANDLE_VALUE;
        OVERLAPPED ov{};
        BYTE buf[256]{};
        int inputReportLen = 0;
        int outputReportLen = 0;
        bool isPending = false;
        std::string path;
    };

    EndpointSlot slots[MAX_ENDPOINTS];
    int numSlots = 0;
    uint32_t localFrameId = 0;
    int consecutiveErrors = 0;

    auto closeAllSlots = [&]() noexcept {
        for (int i = 0; i < numSlots; ++i) {
            if (slots[i].hDevice != INVALID_HANDLE_VALUE) {
                CancelIoEx(slots[i].hDevice, &slots[i].ov);
                CloseHandle(slots[i].hDevice);
                slots[i].hDevice = INVALID_HANDLE_VALUE;
            }
            if (slots[i].ov.hEvent) {
                CloseHandle(slots[i].ov.hEvent);
                slots[i].ov.hEvent = nullptr;
            }
            slots[i].isPending = false;
        }
        numSlots = 0;
        consecutiveErrors = 0;
        g_hDevice = INVALID_HANDLE_VALUE;
        g_numActiveEndpoints.store(0, std::memory_order_relaxed);
        };

    auto dispatchReport = [&](const BYTE* data, int len) noexcept {
        if (!data || len <= 0) return;

        g_rawReportsReceived.fetch_add(1, std::memory_order_relaxed);
        g_lastReportLen.store(len, std::memory_order_relaxed);
        if (len >= 4) {
            uint32_t b4 = static_cast<uint32_t>(data[0]) |
                (static_cast<uint32_t>(data[1]) << 8) |
                (static_cast<uint32_t>(data[2]) << 16) |
                (static_cast<uint32_t>(data[3]) << 24);
            g_lastReportBytes.store(b4, std::memory_order_relaxed);
        }

        int32_t rx = 0, ry = 0;
        bool pressed = false;
        if (DecodeReport(data, len, g_spec, rx, ry, pressed)) {
            LARGE_INTEGER qpc;
            QueryPerformanceCounter(&qpc);
            uint64_t nowQpc = static_cast<uint64_t>(qpc.QuadPart);
            g_lastReportQpc.store(nowQpc, std::memory_order_relaxed);
            g_reportsReceived.fetch_add(1, std::memory_order_relaxed);

            TabletReport rep;
            rep.raw_x = rx;
            rep.raw_y = ry;
            rep.hw_timestamp_qpc = nowQpc;
            rep.frame_id = ++localFrameId;
            rep.pressed = pressed;

            g_reportStore.Store(rep);
            SetEvent(g_reportReadyEvent);
            consecutiveErrors = 0;
        }
        };

    while (!g_exitDriver.load(std::memory_order_relaxed)) {
        if (numSlots == 0) {
            std::vector<std::string> devPaths = DetectAndInitTablet();
            if (devPaths.empty()) {
                if (g_hwnd) SetWindowTextA(g_hwnd, "osu!Point - [Waiting for tablet...]");
                WaitForSingleObject(g_hDeviceChangeEvent, 1000);
                continue;
            }

            for (const auto& path : devPaths) {
                if (numSlots >= MAX_ENDPOINTS) break;
                HANDLE h = OpenTabletHandle(path);
                if (h != INVALID_HANDLE_VALUE) {
                    PHIDP_PREPARSED_DATA ppData = nullptr;
                    int inLen = 0, outLen = 0;
                    if (HidD_GetPreparsedData(h, &ppData)) {
                        HIDP_CAPS caps{};
                        if (HidP_GetCaps(ppData, &caps) == HIDP_STATUS_SUCCESS) {
                            inLen = caps.InputReportByteLength;
                            outLen = caps.OutputReportByteLength;
                        }
                        HidD_FreePreparsedData(ppData);
                    }

                    if (inLen <= 0 && g_spec.report_len > 0) {
                        inLen = g_spec.report_len;
                    }
                    if (inLen <= 0) inLen = 64;

                    slots[numSlots].hDevice = h;
                    slots[numSlots].ov.hEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
                    slots[numSlots].inputReportLen = inLen;
                    slots[numSlots].outputReportLen = outLen;
                    slots[numSlots].isPending = false;
                    slots[numSlots].path = path;
                    HidD_SetNumInputBuffers(h, (numSlots == 0) ? 4 : 16);
                    HidD_FlushQueue(h);
                    numSlots++;
                }
            }

            if (numSlots == 0) {
                WaitForSingleObject(g_hDeviceChangeEvent, 500);
                continue;
            }

            g_hDevice = slots[0].hDevice;
            g_numActiveEndpoints.store(numSlots, std::memory_order_relaxed);

            auto sendTabletWakeHandshake = [&](HANDLE hDev, int outLen) noexcept {
                if (!hDev || hDev == INVALID_HANDLE_VALUE) return;

                // 1. Query vendor string descriptors to trigger tablet MCU initialization
                wchar_t strDesc[256];
                HidD_GetIndexedString(hDev, 2, strDesc, sizeof(strDesc));
                HidD_GetIndexedString(hDev, 100, strDesc, sizeof(strDesc));
                HidD_GetIndexedString(hDev, 110, strDesc, sizeof(strDesc));
                HidD_GetIndexedString(hDev, 200, strDesc, sizeof(strDesc));
                HidD_GetIndexedString(hDev, 201, strDesc, sizeof(strDesc));

                // 2. Feature report initialization
                if (!g_spec.init_feature.empty()) {
                    HidD_SetFeature(hDev, g_spec.init_feature.data(), static_cast<ULONG>(g_spec.init_feature.size()));
                }
                else if (VendorID::IsWacom(g_spec.vid)) {
                    BYTE defWacomFeature[2] = { 0x02, 0x02 };
                    HidD_SetFeature(hDev, defWacomFeature, sizeof(defWacomFeature));
                }

                // 3. Output report handshake
                if (!g_spec.init_output.empty()) {
                    int sendLen = (outLen > 0) ? outLen : static_cast<int>(g_spec.init_output.size());
                    if (sendLen < static_cast<int>(g_spec.init_output.size())) sendLen = static_cast<int>(g_spec.init_output.size());
                    if (sendLen < 8) sendLen = 8;
                    BYTE outBuf[64]{};
                    std::memcpy(outBuf, g_spec.init_output.data(), g_spec.init_output.size());

                    OVERLAPPED ovW{};
                    ovW.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
                    if (ovW.hEvent) {
                        DWORD written = 0;
                        if (!WriteFile(hDev, outBuf, static_cast<DWORD>(sendLen), &written, &ovW)) {
                            if (GetLastError() == ERROR_IO_PENDING) {
                                GetOverlappedResult(hDev, &ovW, &written, TRUE);
                            }
                        }
                        CloseHandle(ovW.hEvent);
                    }

                    HidD_SetOutputReport(hDev, outBuf, static_cast<ULONG>(sendLen));
                    HidD_SetFeature(hDev, outBuf, static_cast<ULONG>(sendLen));
                }

                // 4. VEIKK initialization sequence
                if (VendorID::IsVeikk(g_spec.vid)) {
                    if (outLen >= 9) {
                        BYTE veikkInit[9] = { 0x09, 0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
                        HidD_SetOutputReport(hDev, veikkInit, sizeof(veikkInit));
                    }
                    if (outLen >= 8) {
                        BYTE veikkInit2[8] = { 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
                        HidD_SetOutputReport(hDev, veikkInit2, sizeof(veikkInit2));
                    }
                }
                };

            for (int i = 0; i < numSlots; ++i) {
                sendTabletWakeHandshake(slots[i].hDevice, slots[i].outputReportLen);
                HidD_FlushQueue(slots[i].hDevice);
            }

            TransformConfig currentCfg = g_cfgStore.Load();
            SetTransform(currentCfg.width_mm, currentCfg.height_mm, currentCfg.cx, currentCfg.cy, currentCfg.rotation_deg, currentCfg.monitor_idx);
            LogMessage("Tablet opened: " + g_spec.name + " (" + std::to_string(numSlots) + " endpoints)");
            if (g_spec.is_mouse_mode && !g_hMouseHook) {
                g_hMouseHook = SetWindowsHookExA(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandleA(NULL), 0);
            }
            UpdateStatusText();
        }

        // Keep-alive retry if no reports are received after opening
        static uint64_t s_lastWakeAttemptQpc = 0;
        if (g_rawReportsReceived.load(std::memory_order_relaxed) == 0 && numSlots > 0) {
            LARGE_INTEGER qpcNow;
            QueryPerformanceCounter(&qpcNow);
            if (s_lastWakeAttemptQpc == 0 || (qpcNow.QuadPart - s_lastWakeAttemptQpc) > (g_qpcFreq.QuadPart * 2)) {
                s_lastWakeAttemptQpc = qpcNow.QuadPart;
                for (int i = 0; i < numSlots; ++i) {
                    wchar_t strDesc[256];
                    HidD_GetIndexedString(slots[i].hDevice, 2, strDesc, sizeof(strDesc));
                    HidD_GetIndexedString(slots[i].hDevice, 100, strDesc, sizeof(strDesc));

                    if (!g_spec.init_output.empty()) {
                        int sendLen = (slots[i].outputReportLen > 0) ? slots[i].outputReportLen : static_cast<int>(g_spec.init_output.size());
                        if (sendLen < 8) sendLen = 8;
                        BYTE outBuf[64]{};
                        std::memcpy(outBuf, g_spec.init_output.data(), g_spec.init_output.size());

                        OVERLAPPED ovW{};
                        ovW.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
                        if (ovW.hEvent) {
                            DWORD written = 0;
                            if (!WriteFile(slots[i].hDevice, outBuf, static_cast<DWORD>(sendLen), &written, &ovW)) {
                                if (GetLastError() == ERROR_IO_PENDING) {
                                    GetOverlappedResult(slots[i].hDevice, &ovW, &written, TRUE);
                                }
                            }
                            CloseHandle(ovW.hEvent);
                        }
                        HidD_SetOutputReport(slots[i].hDevice, outBuf, static_cast<ULONG>(sendLen));
                    }
                }
            }
        }

        // Issue asynchronous ReadFile on idle input endpoints
        for (int i = 0; i < numSlots; ++i) {
            if (slots[i].inputReportLen <= 0) continue;

            if (!slots[i].isPending) {
                int bytesToRead = slots[i].inputReportLen;
                if (bytesToRead > static_cast<int>(sizeof(slots[i].buf))) bytesToRead = static_cast<int>(sizeof(slots[i].buf));

                slots[i].ov.Internal = 0;
                slots[i].ov.InternalHigh = 0;
                slots[i].ov.Offset = 0;
                slots[i].ov.OffsetHigh = 0;

                BOOL readOk = ReadFile(slots[i].hDevice, slots[i].buf, bytesToRead, nullptr, &slots[i].ov);
                if (readOk) {
                    // Drain any immediately pending queued reports
                    while (readOk) {
                        DWORD transferred = 0;
                        if (GetOverlappedResult(slots[i].hDevice, &slots[i].ov, &transferred, FALSE)) {
                            dispatchReport(slots[i].buf, static_cast<int>(transferred));
                        }
                        slots[i].ov.Internal = 0;
                        slots[i].ov.InternalHigh = 0;
                        slots[i].ov.Offset = 0;
                        slots[i].ov.OffsetHigh = 0;
                        readOk = ReadFile(slots[i].hDevice, slots[i].buf, bytesToRead, nullptr, &slots[i].ov);
                    }
                    DWORD err = GetLastError();
                    if (err == ERROR_IO_PENDING) {
                        slots[i].isPending = true;
                    }
                    else if (err == ERROR_DEVICE_NOT_CONNECTED || err == ERROR_GEN_FAILURE) {
                        closeAllSlots();
                        g_reconnectCount.fetch_add(1, std::memory_order_relaxed);
                        WaitForSingleObject(g_hDeviceChangeEvent, 1000);
                        break;
                    }
                    else {
                        g_lastReadError.store(err, std::memory_order_relaxed);
                        if (++consecutiveErrors >= 3) {
                            closeAllSlots();
                            g_reconnectCount.fetch_add(1, std::memory_order_relaxed);
                            consecutiveErrors = 0;
                            WaitForSingleObject(g_hDeviceChangeEvent, 1000);
                            break;
                        }
                        Sleep(2);
                        slots[i].isPending = false;
                    }
                }
                else {
                    DWORD err = GetLastError();
                    if (err == ERROR_IO_PENDING) {
                        slots[i].isPending = true;
                    }
                    else if (err == ERROR_DEVICE_NOT_CONNECTED || err == ERROR_GEN_FAILURE) {
                        closeAllSlots();
                        g_reconnectCount.fetch_add(1, std::memory_order_relaxed);
                        WaitForSingleObject(g_hDeviceChangeEvent, 1000);
                        break;
                    }
                    else {
                        g_lastReadError.store(err, std::memory_order_relaxed);
                        if (++consecutiveErrors >= 3) {
                            closeAllSlots();
                            g_reconnectCount.fetch_add(1, std::memory_order_relaxed);
                            consecutiveErrors = 0;
                            WaitForSingleObject(g_hDeviceChangeEvent, 1000);
                            break;
                        }
                        Sleep(2);
                        slots[i].isPending = false;
                    }
                }
            }
        }

        if (numSlots == 0) continue;

        // Wait for I/O completion or device arrival/removal notifications
        HANDLE waitHandles[MAX_ENDPOINTS + 1];
        int waitCount = 0;
        int slotMap[MAX_ENDPOINTS];

        for (int i = 0; i < numSlots; ++i) {
            if (slots[i].isPending) {
                slotMap[waitCount] = i;
                waitHandles[waitCount++] = slots[i].ov.hEvent;
            }
        }
        waitHandles[waitCount] = g_hDeviceChangeEvent;

        if (waitCount == 0) {
            Sleep(2);
            continue;
        }

        // Short spin loop to reduce wake latency during high-frequency input
        constexpr int SPIN_ITERATIONS = 64;
        bool anyCompleted = false;
        for (int spin = 0; spin < SPIN_ITERATIONS; ++spin) {
            _mm_pause();
            for (int w = 0; w < waitCount; ++w) {
                int sIdx = slotMap[w];
                if (HasOverlappedIoCompleted(&slots[sIdx].ov)) {
                    anyCompleted = true;
                    DWORD transferred = 0;
                    if (GetOverlappedResult(slots[sIdx].hDevice, &slots[sIdx].ov, &transferred, FALSE)) {
                        dispatchReport(slots[sIdx].buf, static_cast<int>(transferred));
                        consecutiveErrors = 0;
                    }
                    slots[sIdx].isPending = false;
                }
            }
            if (anyCompleted) break;
        }

        if (anyCompleted) continue;

        DWORD waitRes = WaitForMultipleObjects(waitCount + 1, waitHandles, FALSE, 250);
        if (waitRes >= WAIT_OBJECT_0 && waitRes < (WAIT_OBJECT_0 + waitCount)) {
            int slotIdx = slotMap[waitRes - WAIT_OBJECT_0];
            DWORD transferred = 0;
            if (GetOverlappedResult(slots[slotIdx].hDevice, &slots[slotIdx].ov, &transferred, FALSE)) {
                dispatchReport(slots[slotIdx].buf, static_cast<int>(transferred));
                consecutiveErrors = 0;
            }
            slots[slotIdx].isPending = false;
        }
        else if (waitRes == (WAIT_OBJECT_0 + waitCount)) {
            closeAllSlots();
            continue;
        }
    }

    closeAllSlots();

    if (hMmcss) {
        AvRevertMmThreadCharacteristics(hMmcss);
    }
}

// Processing thread: transforms tablet coordinates and injects mouse input
void ProcessingThread() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    if (g_affinityPair.processor != 0) {
        SetThreadAffinityMask(GetCurrentThread(), g_affinityPair.processor);
        SetThreadIdealProcessor(GetCurrentThread(), g_affinityPair.processorCore);
    }

    DisablePowerThrottling();
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);

    DWORD taskIndex = 0;
    HANDLE hMmcss = AvSetMmThreadCharacteristicsA("Pro Audio", &taskIndex);
    if (hMmcss) {
        AvSetMmThreadPriority(hMmcss, AVRT_PRIORITY_CRITICAL);
    }

    INPUT mouseInputs[2] = { 0 };
    mouseInputs[0].type = INPUT_MOUSE;
    mouseInputs[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    mouseInputs[1].type = INPUT_MOUSE;

    // Packed [dx, dy] 64-bit integer representation for fast deduplication
    uint64_t lastPackedDxy = 0xFFFFFFFFFFFFFFFFULL;
    uint32_t lastProcessedFrameId = 0;
    bool lastPressed = false;
    int inProximityFrames = 0;

    uint32_t localSeq = 0xFFFFFFFF;
    TransformConfig localCfg{};

    const __m128d vZero = _mm_setzero_pd();
    const __m128d vOne = _mm_set1_pd(1.0);
    const __m128d vMaxVal = _mm_set1_pd(65535.0);

    __m128d vknx = _mm_setzero_pd();
    __m128d vkny = _mm_setzero_pd();
    __m128d vknc = _mm_setzero_pd();
    __m128d vvscale = _mm_set1_pd(65535.0);
    __m128d vvshift = _mm_set1_pd(0.5);

    int idleSpins = 0;
    bool wasOutside = false;

    while (!g_exitDriver.load(std::memory_order_relaxed)) {

        uint32_t peekFid = g_reportStore.PeekFrameId();

        if (peekFid == lastProcessedFrameId || peekFid == 0) {
            ++idleSpins;
            if (idleSpins <= 128) {
                // Short spin wait for incoming packets (< 1.5 µs)
                _mm_pause();
            }
            else {
                // Sleep until next report event or timeout
                WaitForSingleObject(g_reportReadyEvent, 1);
                idleSpins = 0;
            }

            // Watchdog: release mouse button if pen lifts abruptly without sending a release packet
            LARGE_INTEGER qpcNow;
            QueryPerformanceCounter(&qpcNow);
            uint64_t lastQpc = g_lastReportQpc.load(std::memory_order_relaxed);
            if (lastQpc != 0 && (static_cast<uint64_t>(qpcNow.QuadPart) - lastQpc) > (static_cast<uint64_t>(g_qpcFreq.QuadPart) * 35 / 1000)) {
                if (lastPressed) {
                    INPUT upInput = { 0 };
                    upInput.type = INPUT_MOUSE;
                    upInput.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                    SendInput(1, &upInput, sizeof(INPUT));
                    lastPressed = false;
                }
                inProximityFrames = 0;
            }
            continue;
        }

        TabletReport report = g_reportStore.Load();
        if (report.frame_id == lastProcessedFrameId) {
            _mm_pause();
            continue;
        }

        idleSpins = 0;
        lastProcessedFrameId = report.frame_id;

        // Refresh transformation matrix when configuration updates
        uint32_t curSeq = g_cfgStore.GetSequence();
        if (curSeq != localSeq) [[unlikely]] {
            localCfg = g_cfgStore.Load();
            localSeq = curSeq;
            vknx = _mm_load_pd(localCfg.knx);
            vkny = _mm_load_pd(localCfg.kny);
            vknc = _mm_load_pd(localCfg.knc);
            vvscale = _mm_load_pd(localCfg.vscale);
            vvshift = _mm_load_pd(localCfg.vshift);
            wasOutside = false;
        }

        // SIMD evaluation of normalized coordinates [un, vn]
        const __m128d vfx = _mm_set1_pd(static_cast<double>(report.raw_x));
        const __m128d vfy = _mm_set1_pd(static_cast<double>(report.raw_y));

#if defined(__AVX2__) || defined(__FMA__)
        __m128d vn = _mm_fmadd_pd(vfx, vknx, _mm_fmadd_pd(vfy, vkny, vknc));
#else
        __m128d vn = _mm_add_pd(_mm_mul_pd(vfx, vknx), _mm_add_pd(_mm_mul_pd(vfy, vkny), vknc));
#endif

        if (localCfg.clip_area) {
            // Strict area clipping: clamp initial exit packet to boundary, drop subsequent outside packets
            const __m128d cmpLo = _mm_cmplt_pd(vn, vZero);
            const __m128d cmpHi = _mm_cmpgt_pd(vn, vOne);
            const bool isOutside = (_mm_movemask_pd(_mm_or_pd(cmpLo, cmpHi)) != 0);

            if (isOutside) [[unlikely]] {
                if (wasOutside) {
                    continue;
                }
                wasOutside = true;
                vn = _mm_min_pd(_mm_max_pd(vn, vZero), vOne);
            }
            else {
                wasOutside = false;
            }
        }
        else {
            // Clamped mode: clamp coordinates directly to boundaries without dropping packets
            wasOutside = false;
            vn = _mm_min_pd(_mm_max_pd(vn, vZero), vOne);
        }

        // Map normalized coordinates to absolute screen space [0, 65535]
#if defined(__AVX2__) || defined(__FMA__)
        __m128d vout = _mm_fmadd_pd(vn, vvscale, vvshift);
#else
        __m128d vout = _mm_add_pd(_mm_mul_pd(vn, vvscale), vvshift);
#endif
        vout = _mm_min_pd(_mm_max_pd(vout, vZero), vMaxVal);

        // Convert double coordinates to 32-bit integers
        const __m128i vi = _mm_cvttpd_epi32(vout);
        const uint64_t packedDxy = static_cast<uint64_t>(_mm_cvtsi128_si64(vi));

        if (inProximityFrames < 3) {
            inProximityFrames++;
        }

        bool curPressed = (inProximityFrames >= 3) && report.pressed && g_penClick.load(std::memory_order_relaxed);
        bool clickEdge = (curPressed != lastPressed);

        // Discard redundant coordinate updates unless a click event must be processed
        if (packedDxy == lastPackedDxy && !clickEdge) [[unlikely]] {
            g_reportsDeduped.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        lastPackedDxy = packedDxy;

        std::memcpy(&mouseInputs[0].mi.dx, &packedDxy, sizeof(uint64_t));

        UINT inputCount = 1;
        if (clickEdge) {
            mouseInputs[1].mi.dwFlags = curPressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            inputCount = 2;
            lastPressed = curPressed;
        }

        // Direct hardware input injection
        SendInput(inputCount, mouseInputs, sizeof(INPUT));
    }

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
    }
    else {
        snprintf(buf, sizeof(buf), "%d:%d", rw, rh);
    }

    SetWindowTextA(hRatio, buf);
    g_lockRatio = true;
    if (chkRatio) InvalidateRect(chkRatio, NULL, FALSE);

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
                    }
                    else if (d <= 7.0f) {
                        r_sum += 28; g_sum += 28; b_sum += 34; a_sum += 255;
                    }
                    else if (d <= 14.5f) {
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
    if (g_lockDrag) return -1;

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

    // Card frame
    HBRUSH cardBrush = CreateSolidBrush(COLOR_CARD);
    HPEN borderPen = CreatePen(PS_SOLID, 1, COLOR_BORDER);
    HGDIOBJ oldBrush = SelectObject(hdc, cardBrush);
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    Rectangle(hdc, PREVIEW_X - 5, PREVIEW_Y - 5, PREVIEW_X + tab_w + 5, PREVIEW_Y + tab_h + 5);

    // Tablet active area background
    HBRUSH tabBrush = CreateSolidBrush(RGB(28, 28, 36));
    HPEN tabPen = CreatePen(PS_SOLID, 1, RGB(42, 42, 54));
    SelectObject(hdc, tabBrush);
    SelectObject(hdc, tabPen);
    Rectangle(hdc, PREVIEW_X, PREVIEW_Y, PREVIEW_X + tab_w, PREVIEW_Y + tab_h);
    DeleteObject(tabBrush);
    DeleteObject(tabPen);

    // Tablet corner guides
    HPEN tickPen = CreatePen(PS_SOLID, 1, RGB(65, 65, 82));
    SelectObject(hdc, tickPen);
    int ox = g_originX;
    int oy = g_originY;
    int ow = active_w;
    int oh = active_h;

    MoveToEx(hdc, ox, oy + 5, nullptr); LineTo(hdc, ox, oy); LineTo(hdc, ox + 6, oy);
    MoveToEx(hdc, ox + ow - 5, oy, nullptr); LineTo(hdc, ox + ow, oy); LineTo(hdc, ox + ow, oy + 6);
    MoveToEx(hdc, ox, oy + oh - 5, nullptr); LineTo(hdc, ox, oy + oh); LineTo(hdc, ox + 6, oy + oh);
    MoveToEx(hdc, ox + ow - 5, oy + oh, nullptr); LineTo(hdc, ox + ow, oy + oh); LineTo(hdc, ox + ow, oy + oh - 6);
    DeleteObject(tickPen);

    TransformConfig cfg = g_cfgStore.Load();

    // Area dimension readout
    char dimBuf[48];
    snprintf(dimBuf, sizeof(dimBuf), "%.1f x %.1f mm", cfg.width_mm, cfg.height_mm);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(95, 95, 115));
    HGDIOBJ oldF = SelectObject(hdc, g_hFontSmall);
    RECT dimRc{ ox, oy + oh - 16, ox + ow - 4, oy + oh };
    DrawTextA(hdc, dimBuf, -1, &dimRc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldF);

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

    // Active area polygon
    HBRUSH areaBrush = CreateSolidBrush(COLOR_ACCENT);
    HPEN areaPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    SelectObject(hdc, areaBrush);
    SelectObject(hdc, areaPen);

    Polygon(hdc, g_previewCorners, 4);

    // Resize and position control handles
    HBRUSH handleBrush = CreateSolidBrush(RGB(255, 255, 255));
    SelectObject(hdc, handleBrush);
    SelectObject(hdc, GetStockObject(NULL_PEN));

    if (!g_lockDrag) {
        for (int i = 0; i < 4; ++i) {
            Ellipse(hdc, g_previewCorners[i].x - 3, g_previewCorners[i].y - 3,
                g_previewCorners[i].x + 4, g_previewCorners[i].y + 4);
        }
    }

    // Center handle
    Ellipse(hdc, g_previewCenter.x - 3, g_previewCenter.y - 3,
        g_previewCenter.x + 4, g_previewCenter.y + 4);

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

    g_exitDriver.store(true, std::memory_order_release);

    if (g_hDevice != INVALID_HANDLE_VALUE) {
        CancelIoEx(g_hDevice, nullptr);
    }

    if (g_hDeviceChangeEvent) {
        SetEvent(g_hDeviceChangeEvent);
    }

    if (g_reportReadyEvent) {
        SetEvent(g_reportReadyEvent);
    }

    if (g_hDevNotify) {
        UnregisterDeviceNotification(g_hDevNotify);
        g_hDevNotify = NULL;
    }

    if (g_readerThread.joinable()) {
        g_readerThread.join();
    }
    if (g_processingThread.joinable()) {
        g_processingThread.join();
    }

    if (g_reportReadyEvent) {
        CloseHandle(g_reportReadyEvent);
        g_reportReadyEvent = NULL;
    }

    DisableSubMillisecondTimer();
    SaveConfig();

    if (g_nid.hWnd) {
        Shell_NotifyIconA(NIM_DELETE, &g_nid);
        g_nid.hWnd = NULL;
    }

    if (g_hDeviceChangeEvent) { CloseHandle(g_hDeviceChangeEvent); g_hDeviceChangeEvent = NULL; }

    if (g_hMouseHook) {
        UnhookWindowsHookEx(g_hMouseHook);
        g_hMouseHook = NULL;
    }

    PostQuitMessage(0);
}

LRESULT CALLBACK CboSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rc;
        GetClientRect(hWnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBm = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ oldBm = SelectObject(memDC, memBm);

        HBRUSH bgBrush = CreateSolidBrush(COLOR_EDIT_BG);
        FillRect(memDC, &rc, bgBrush);
        DeleteObject(bgBrush);

        HPEN borderPen = CreatePen(PS_SOLID, 1, COLOR_BORDER);
        HGDIOBJ oldPen = SelectObject(memDC, borderPen);
        HGDIOBJ oldBrush = SelectObject(memDC, GetStockObject(NULL_BRUSH));
        Rectangle(memDC, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(memDC, oldBrush);
        SelectObject(memDC, oldPen);
        DeleteObject(borderPen);

        int curSel = static_cast<int>(CallWindowProcA(g_origCboProc, hWnd, CB_GETCURSEL, 0, 0));
        char textBuf[128] = "";
        if (curSel != CB_ERR) {
            CallWindowProcA(g_origCboProc, hWnd, CB_GETLBTEXT, curSel, reinterpret_cast<LPARAM>(textBuf));
        }

        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, COLOR_TEXT);
        HGDIOBJ oldFont = SelectObject(memDC, g_hFont);

        RECT textRc = rc;
        textRc.left += 8;
        textRc.right -= 20;
        DrawTextA(memDC, textBuf, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(memDC, oldFont);

        int arrowX = rc.right - 11;
        int arrowY = (rc.top + rc.bottom) / 2;
        POINT arrowPts[3] = {
            { arrowX - 4, arrowY - 2 },
            { arrowX + 4, arrowY - 2 },
            { arrowX,     arrowY + 3 }
        };
        HBRUSH arrowBrush = CreateSolidBrush(COLOR_TEXT_MUTED);
        HPEN arrowPen = CreatePen(PS_SOLID, 1, COLOR_TEXT_MUTED);
        HGDIOBJ ob = SelectObject(memDC, arrowBrush);
        HGDIOBJ op = SelectObject(memDC, arrowPen);
        Polygon(memDC, arrowPts, 3);
        SelectObject(memDC, ob);
        SelectObject(memDC, op);
        DeleteObject(arrowBrush);
        DeleteObject(arrowPen);

        BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBm);
        DeleteObject(memBm);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
        return 0;
    }
    if (msg == WM_NCPAINT || msg == WM_ERASEBKGND) {
        return 0;
    }
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
        InvalidateRect(hWnd, NULL, FALSE);
    }
    return CallWindowProcA(g_origCboProc, hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_INPUT) {
        // Fallback input path: handle raw input only if dedicated ReaderThread is idle
        LARGE_INTEGER qpcNow;
        QueryPerformanceCounter(&qpcNow);
        uint64_t lastQpc = g_lastReportQpc.load(std::memory_order_relaxed);
        if (g_numActiveEndpoints.load(std::memory_order_relaxed) > 0) {
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        UINT dwSize = 0;
        GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &dwSize, sizeof(RAWINPUTHEADER));
        if (dwSize > 0 && dwSize <= 512) {
            alignas(RAWINPUT) BYTE rawBuf[512];
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, rawBuf, &dwSize, sizeof(RAWINPUTHEADER)) == dwSize) {
                auto* raw = reinterpret_cast<RAWINPUT*>(rawBuf);
                if (raw->header.dwType == RIM_TYPEHID) {
                    BYTE* data = raw->data.hid.bRawData;
                    int len = static_cast<int>(raw->data.hid.dwSizeHid);
                    if (data && len > 0) {
                        int32_t rx = 0, ry = 0;
                        bool pressed = false;
                        if (DecodeReport(data, len, g_spec, rx, ry, pressed)) {
                            uint64_t nowQpc = static_cast<uint64_t>(qpcNow.QuadPart);
                            g_lastReportQpc.store(nowQpc, std::memory_order_relaxed);
                            g_rawReportsReceived.fetch_add(1, std::memory_order_relaxed);
                            g_reportsReceived.fetch_add(1, std::memory_order_relaxed);

                            TabletReport rep;
                            rep.raw_x = rx;
                            rep.raw_y = ry;
                            rep.hw_timestamp_qpc = nowQpc;
                            rep.pressed = pressed;
                            static std::atomic<uint32_t> s_rawInputFrameId{ 0 };
                            rep.frame_id = s_rawInputFrameId.fetch_add(1, std::memory_order_relaxed) + 1;

                            g_reportStore.Store(rep);
                            SetEvent(g_reportReadyEvent);
                        }
                    }
                }
            }
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
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
        static int s_timerTicks = 0;
        if (++s_timerTicks >= 2) {
            s_timerTicks = 0;
            UpdateStatusText();
        }
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
        }
        else if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            InsertMenuA(hMenu, 0, MF_BYPOSITION | MF_STRING, IDM_TRAY_OPEN, "Open osu!Point");
            InsertMenuA(hMenu, 1, MF_BYPOSITION | MF_STRING | (g_penClick.load(std::memory_order_relaxed) ? MF_CHECKED : MF_UNCHECKED), IDM_TRAY_PENCLICK, "Pen Click");
            InsertMenuA(hMenu, 2, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
            InsertMenuA(hMenu, 3, MF_BYPOSITION | MF_STRING, IDM_TRAY_EXIT, "Exit");

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
            }
            else if (hit == 1 || hit == 3) {
                SetCursor(LoadCursor(NULL, IDC_SIZENESW));
                return TRUE;
            }
            else if (hit == 4 || hit == 5) {
                SetCursor(LoadCursor(NULL, IDC_SIZEALL));
                return TRUE;
            }
            else {
                SetCursor(LoadCursor(NULL, IDC_ARROW));
                return TRUE;
            }
        }
    }
    if (msg == WM_LBUTTONDOWN) {
        if (!g_lockDrag) {
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
        }
        else {
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

            if (g_lockRatio) {
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
            }
            else {
                g_target_monitor_idx = -1;
            }
            InvalidateRect(hCboMon, NULL, TRUE);
            UpdateValues();
            UpdateStatusText();
        }
        if (LOWORD(wParam) == 1004) {
            g_lockDrag = !g_lockDrag;
            InvalidateRect(chkLockDrag, NULL, FALSE);
            InvalidateRect(hwnd, NULL, FALSE);
            SaveConfig();
        }
        if (LOWORD(wParam) == 1005) {
            g_clipArea = !g_clipArea;
            InvalidateRect(chkClipArea, NULL, FALSE);
            UpdateValues();
            SaveConfig();
        }
        if (LOWORD(wParam) == 1006 || LOWORD(wParam) == IDM_TRAY_PENCLICK) {
            bool nextState = !g_penClick.load(std::memory_order_relaxed);
            g_penClick.store(nextState, std::memory_order_relaxed);
            if (chkPenClick) InvalidateRect(chkPenClick, NULL, FALSE);
            SaveConfig();
        }
        if (LOWORD(wParam) == 1003) {
            AutoMatchRatio();
        }
        if (LOWORD(wParam) == 1002) {
            g_lockRatio = !g_lockRatio;
            InvalidateRect(chkRatio, NULL, FALSE);
            if (g_lockRatio) {
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
            if (g_lockRatio && (src == hW || src == hH || src == hRatio)) {
                char rBuf[32];
                GetWindowTextA(hRatio, rBuf, 32);
                double ratio = ParseAspectRatio(rBuf);
                g_isUpdatingUI = true;
                if (src == hW || src == hRatio) {
                    double cw = ParseInput(hW);
                    SetWindowTextA(hH, FormatDouble(cw / ratio).c_str());
                }
                else if (src == hH) {
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
    if (msg == WM_CTLCOLORSTATIC) {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, COLOR_TEXT_MUTED);
        SetBkColor(hdc, COLOR_BG);
        return reinterpret_cast<LRESULT>(g_hBrushBg);
    }
    if (msg == WM_DRAWITEM) {
        auto pDIS = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);

        // Owner-drawn buttons
        if (pDIS->CtlID == 1001 || pDIS->CtlID == 1003) {
            FillRect(pDIS->hDC, &pDIS->rcItem, g_hBrushBg);

            bool pressed = (pDIS->itemState & ODS_SELECTED);
            HBRUSH btnBrush = CreateSolidBrush(pressed ? COLOR_BTN_HOVER : COLOR_BTN);
            HPEN btnPen = CreatePen(PS_SOLID, 1, pressed ? COLOR_ACCENT : COLOR_BORDER_ACC);
            HGDIOBJ ob = SelectObject(pDIS->hDC, btnBrush);
            HGDIOBJ op = SelectObject(pDIS->hDC, btnPen);

            Rectangle(pDIS->hDC, pDIS->rcItem.left, pDIS->rcItem.top, pDIS->rcItem.right, pDIS->rcItem.bottom);

            SetBkMode(pDIS->hDC, TRANSPARENT);
            SetTextColor(pDIS->hDC, pressed ? COLOR_ACCENT : COLOR_TEXT);
            SelectObject(pDIS->hDC, (pDIS->CtlID == 1003) ? g_hFontSmall : g_hFont);

            const char* label = (pDIS->CtlID == 1001) ? "Full Area (Reset)" : "Auto";
            DrawTextA(pDIS->hDC, label, -1, &pDIS->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(pDIS->hDC, ob);
            SelectObject(pDIS->hDC, op);
            DeleteObject(btnBrush);
            DeleteObject(btnPen);
            return TRUE;
        }

        // Owner-drawn checkboxes
        if (pDIS->CtlID == 1002 || pDIS->CtlID == 1004 || pDIS->CtlID == 1005 || pDIS->CtlID == 1006) {
            FillRect(pDIS->hDC, &pDIS->rcItem, g_hBrushBg);

            bool isChecked = false;
            const char* label = "";
            if (pDIS->CtlID == 1002) { isChecked = g_lockRatio; label = "Ratio:"; }
            else if (pDIS->CtlID == 1004) { isChecked = g_lockDrag; label = "Lock Area"; }
            else if (pDIS->CtlID == 1005) { isChecked = g_clipArea; label = "Clip Area"; }
            else if (pDIS->CtlID == 1006) { isChecked = g_penClick.load(std::memory_order_relaxed); label = "Pen Click"; }

            int boxY = (pDIS->rcItem.bottom - pDIS->rcItem.top - 14) / 2;
            RECT boxRc = { 0, boxY, 14, boxY + 14 };

            HBRUSH boxBg = CreateSolidBrush(COLOR_EDIT_BG);
            HPEN boxPen = CreatePen(PS_SOLID, 1, isChecked ? COLOR_ACCENT : COLOR_BORDER);
            HGDIOBJ ob = SelectObject(pDIS->hDC, boxBg);
            HGDIOBJ op = SelectObject(pDIS->hDC, boxPen);

            Rectangle(pDIS->hDC, boxRc.left, boxRc.top, boxRc.right, boxRc.bottom);

            if (isChecked) {
                RECT indRc = { boxRc.left + 3, boxRc.top + 3, boxRc.right - 3, boxRc.bottom - 3 };
                HBRUSH accBrush = CreateSolidBrush(COLOR_ACCENT);
                FillRect(pDIS->hDC, &indRc, accBrush);
                DeleteObject(accBrush);
            }

            SelectObject(pDIS->hDC, ob);
            SelectObject(pDIS->hDC, op);
            DeleteObject(boxBg);
            DeleteObject(boxPen);

            SetBkMode(pDIS->hDC, TRANSPARENT);
            SetTextColor(pDIS->hDC, isChecked ? COLOR_TEXT : COLOR_TEXT_MUTED);
            SelectObject(pDIS->hDC, g_hFont);

            RECT txtRc = pDIS->rcItem;
            txtRc.left += 20;
            DrawTextA(pDIS->hDC, label, -1, &txtRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

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

    QueryPerformanceFrequency(&g_qpcFreq);

    if (g_qpcFreq.QuadPart > 0) {
        g_qpcToUs = 1e6 / static_cast<double>(g_qpcFreq.QuadPart);
    }

    g_affinityPair = CalculateDualAffinity();

    // Assign UI thread away from real-time processing cores
    if (g_affinityPair.ui != 0) {
        SetThreadAffinityMask(GetCurrentThread(), g_affinityPair.ui);
    }

    g_hDeviceChangeEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    g_reportReadyEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);

    g_baseDir = GetExeDirectory();
    RefreshMonitors();
    DetectAndInitTablet();
    if (g_spec.is_mouse_mode && !g_hMouseHook) {
        g_hMouseHook = SetWindowsHookExA(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandleA(NULL), 0);
    }
    LoadConfig();

    g_hBrushBg = CreateSolidBrush(COLOR_BG);
    g_hBrushEdit = CreateSolidBrush(COLOR_EDIT_BG);

    g_hFont = CreateFontA(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, "Bahnschrift");
    if (!g_hFont) {
        g_hFont = CreateFontA(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    }

    g_hFontSmall = CreateFontA(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, "Bahnschrift");
    if (!g_hFontSmall) {
        g_hFontSmall = CreateFontA(12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    }

    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hbrBackground = g_hBrushBg;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconA(hInst, MAKEINTRESOURCEA(1));
    wc.lpszClassName = "osuPoint_Driver";
    RegisterClassA(&wc);

    DWORD winStyle = WS_OVERLAPPEDWINDOW ^ (WS_THICKFRAME | WS_MAXIMIZEBOX);
    RECT winRc = { 0, 0, 555, 282 };
    AdjustWindowRect(&winRc, winStyle, FALSE);
    int winWidth = winRc.right - winRc.left;
    int winHeight = winRc.bottom - winRc.top;

    g_hwnd = CreateWindowA("osuPoint_Driver", "osu!Point",
        winStyle, CW_USEDEFAULT, CW_USEDEFAULT, winWidth, winHeight,
        NULL, NULL, hInst, NULL);

    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    DEV_BROADCAST_DEVICEINTERFACE_A devFilter{};
    devFilter.dbcc_size = sizeof(devFilter);
    devFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    devFilter.dbcc_classguid = hidGuid;
    g_hDevNotify = RegisterDeviceNotificationA(g_hwnd, &devFilter, DEVICE_NOTIFY_WINDOW_HANDLE);

    // Register raw input device notifications for fallback input acquisition
    RAWINPUTDEVICE rid[2]{};
    rid[0].usUsagePage = 0x0D; // Digitizer
    rid[0].usUsage = 0x02; // Pen
    rid[0].dwFlags = RIDEV_INPUTSINK;
    rid[0].hwndTarget = g_hwnd;

    rid[1].usUsagePage = 0xFF0D; // Vendor-specific digitizer
    rid[1].usUsage = 0x02;
    rid[1].dwFlags = RIDEV_INPUTSINK;
    rid[1].hwndTarget = g_hwnd;

    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));

    BOOL useDarkMode = TRUE;
    if (FAILED(DwmSetWindowAttribute(g_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode)))) {
        DwmSetWindowAttribute(g_hwnd, 19, &useDarkMode, sizeof(useDarkMode));
    }

    HICON hIcon = wc.hIcon;
    if (!hIcon) {
        hIcon = CreateAppIcon();
    }
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
    hCboMon = CreateWindowA("COMBOBOX", "", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 74, 11, 141, 140, g_hwnd, NULL, NULL, NULL);
    SendMessage(hCboMon, CB_SETDROPPEDWIDTH, 170, 0);

    g_origCboProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(hCboMon, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(CboSubclassProc)));

    for (size_t i = 0; i < g_monitors.size(); ++i) {
        char mDesc[64];
        int mw = g_monitors[i].rc.right - g_monitors[i].rc.left;
        int mh = g_monitors[i].rc.bottom - g_monitors[i].rc.top;
        snprintf(mDesc, sizeof(mDesc), "#%d (%dx%d)%s", static_cast<int>(i + 1), mw, mh, g_monitors[i].isPrimary ? " *" : "");
        SendMessageA(hCboMon, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(mDesc));
    }
    SendMessageA(hCboMon, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Entire Desktop"));

    if (g_target_monitor_idx >= 0 && g_target_monitor_idx < static_cast<int>(g_monitors.size())) {
        SendMessage(hCboMon, CB_SETCURSEL, g_target_monitor_idx, 0);
    }
    else {
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

    chkRatio = CreateWindowA("BUTTON", "Ratio:", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 15, 180, 62, 22, g_hwnd, reinterpret_cast<HMENU>(1002), NULL, NULL);
    hRatio = CreateWindowA("EDIT", "16:9", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 80, 180, 75, 22, g_hwnd, NULL, NULL, NULL);
    btnAutoRatio = CreateWindowA("BUTTON", "Auto", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 162, 180, 53, 22, g_hwnd, reinterpret_cast<HMENU>(1003), NULL, NULL);

    chkLockDrag = CreateWindowA("BUTTON", "Lock Area", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 15, 206, 96, 20, g_hwnd, reinterpret_cast<HMENU>(1004), NULL, NULL);
    chkClipArea = CreateWindowA("BUTTON", "Clip Area", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 115, 206, 100, 20, g_hwnd, reinterpret_cast<HMENU>(1005), NULL, NULL);
    chkPenClick = CreateWindowA("BUTTON", "Pen Click", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 15, 228, 96, 20, g_hwnd, reinterpret_cast<HMENU>(1006), NULL, NULL);

    HWND btnReset = CreateWindowA("BUTTON", "Full Area (Reset)", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 15, 252, 200, 24, g_hwnd, reinterpret_cast<HMENU>(1001), NULL, NULL);

    HWND uiControls[] = { lblMon, hCboMon, lblW, hW, lblH, hH, lblCX, hCX, lblCY, hCY, lblRot, hRot, chkRatio, hRatio, btnAutoRatio, chkLockDrag, chkClipArea, chkPenClick, btnReset };
    for (HWND c : uiControls) {
        SendMessage(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFont), TRUE);
    }
    SendMessage(btnAutoRatio, WM_SETFONT, reinterpret_cast<WPARAM>(g_hFontSmall), TRUE);

    g_guiReady = true;
    UpdateValues();
    UpdateStatusText();

    SetTimer(g_hwnd, 1, 200, NULL);

    ShowWindow(g_hwnd, nCmdShow);

    g_readerThread = std::thread(ReaderThread);
    g_processingThread = std::thread(ProcessingThread);

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
