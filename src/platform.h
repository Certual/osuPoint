#pragma once
#include "common.h"

using PFN_NtUserSendInput = UINT (WINAPI*)(UINT cInputs, LPINPUT pInputs, int cbSize);
extern PFN_NtUserSendInput g_pfnNtUserSendInput;

void InitSendInputSyscall() noexcept;

__forceinline UINT DirectSendInput(UINT cInputs, LPINPUT pInputs, int cbSize) noexcept {
    if (g_pfnNtUserSendInput) [[likely]] {
        return g_pfnNtUserSendInput(cInputs, pInputs, cbSize);
    }
    return SendInput(cInputs, pInputs, cbSize);
}

void EnablePerMonitorDpi() noexcept;
void EnableSubMillisecondTimer() noexcept;
void DisableSubMillisecondTimer() noexcept;
void DisablePowerThrottling() noexcept;

struct AffinityConfig {
    DWORD_PTR reader = 0;
    DWORD_PTR dispatcher = 0;
    DWORD_PTR ui = 0;           // UI/WinMain thread mask (core 0 or lowest available)
    DWORD   readerCore = 0;     // Logical processor index for reader
    DWORD   dispatcherCore = 0; // Logical processor index for dispatcher
};

AffinityConfig CalculateThreadAffinity() noexcept;
extern AffinityConfig g_affinityConfig;

std::string GetConflictingDriverProcess();
extern std::atomic<bool> g_needsConflictWarning;

COLORREF GetWindowsSystemAccentColor() noexcept;
bool IsWindowsAppsLightTheme() noexcept;
void UpdateSystemAccentPalette() noexcept;
struct ConsoleTelemetry {
    bool connected = false;
    std::string deviceName;
    uint16_t vid = 0;
    uint16_t pid = 0;
    double phys_w = 0.0;
    double phys_h = 0.0;
    uint32_t max_raw_x = 0;
    uint32_t max_raw_y = 0;
    uint32_t max_pressure = 0;

    int monIdx = 0;
    int monW = 0;
    int monH = 0;
    int monX = 0;
    int monY = 0;
    int deskW = 0;
    int deskH = 0;

    double areaW = 0.0;
    double areaH = 0.0;
    double areaCX = 0.0;
    double areaCY = 0.0;
    double areaRot = 0.0;
    std::string areaRatio;
    bool clipArea = false;
    bool lockDrag = false;

    int32_t penRawX = 0;
    int32_t penRawY = 0;
    int32_t penPx = 0;
    int32_t penPy = 0;
    int32_t penSiX = 0;
    int32_t penSiY = 0;
    double penUn = 0.0;
    double penVn = 0.0;
    uint16_t penPressure = 0;
    bool penTip = false;
    bool penBarrel = false;
    bool penInRange = false;

    struct TrajPoint {
        int32_t rx = 0;
        int32_t ry = 0;
        int32_t px = 0;
        int32_t py = 0;
        uint16_t p = 0;
        bool tip = false;
    } history[4]{};
    size_t historyCount = 0;

    double hz = 0.0;
    int ep = 0;
    uint64_t okCount = 0;
    uint64_t rejCount = 0;
    double dedupPct = 0.0;
    bool extrapEnabled = false;
    double extrapTau = 0.0;
    uint32_t lat_p50 = 0;
    uint32_t lat_p99 = 0;
    uint32_t lat_max = 0;
    uint32_t jit = 0;
    DWORD lastErr = 0;
    int connCount = 0;
};

void SetupDebugConsole(bool enable = true) noexcept;
void InitPinnedConsoleHeader() noexcept;
void UpdatePinnedTelemetry(const ConsoleTelemetry& telem) noexcept;
void ConsoleLock() noexcept;
void ConsoleUnlock() noexcept;

