#include "platform.h"
#include "config_manager.h"
#include "tablet_spec.h"
#include <io.h>
#include <fcntl.h>

extern TabletSpec g_spec;

PFN_NtUserSendInput g_pfnNtUserSendInput = nullptr;
AffinityConfig g_affinityConfig;
std::atomic<bool> g_needsConflictWarning{ false };

static bool s_ntTimerSet = false;

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

#ifndef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
#endif
#ifndef THREAD_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
#define THREAD_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
#endif

void InitSendInputSyscall() noexcept {
    HMODULE hWin32u = LoadLibraryA("win32u.dll");
    if (hWin32u) {
        g_pfnNtUserSendInput = reinterpret_cast<PFN_NtUserSendInput>(GetProcAddress(hWin32u, "NtUserSendInput"));
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
    timeBeginPeriod(1);
    HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
    if (hNtdll) {
        auto NtSetTimerResolution = reinterpret_cast<pfnNtSetTimerResolution>(GetProcAddress(hNtdll, "NtSetTimerResolution"));
        if (NtSetTimerResolution) {
            ULONG currentRes = 0;
            if (NtSetTimerResolution(5000, TRUE, &currentRes) == 0) {
                s_ntTimerSet = true;
            }
        }
    }
}

void DisableSubMillisecondTimer() noexcept {
    if (s_ntTimerSet) {
        HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
        if (hNtdll) {
            auto NtSetTimerResolution = reinterpret_cast<pfnNtSetTimerResolution>(GetProcAddress(hNtdll, "NtSetTimerResolution"));
            if (NtSetTimerResolution) {
                ULONG currentRes = 0;
                NtSetTimerResolution(5000, FALSE, &currentRes);
            }
        }
        s_ntTimerSet = false;
    }
    timeEndPeriod(1);
}

void DisablePowerThrottling() noexcept {
    PROCESS_POWER_THROTTLING_STATE ppts{};
    ppts.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    ppts.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    ppts.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &ppts, sizeof(ppts));

    THREAD_POWER_THROTTLING_STATE tpts{};
    tpts.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    tpts.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED | THREAD_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    tpts.StateMask = 0;
    SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &tpts, sizeof(tpts));
}

AffinityConfig CalculateThreadAffinity() noexcept {
    AffinityConfig result;

    DWORD_PTR processAffinity = 0, systemAffinity = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &processAffinity, &systemAffinity) || processAffinity == 0) {
        return result;
    }

    DWORD returnLength = 0;
    GetLogicalProcessorInformationEx(RelationAll, nullptr, &returnLength);
    if (returnLength == 0) return result;

    std::vector<BYTE> buffer(returnLength);
    if (!GetLogicalProcessorInformationEx(RelationAll,
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

    // Identify L3 cache masks
    DWORD_PTR l3Masks[16] = {};
    int l3Count = 0;
    {
        BYTE* ptr = buffer.data();
        BYTE* end = ptr + returnLength;
        while (ptr < end && l3Count < 16) {
            auto cur = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(ptr);
            if (cur->Relationship == RelationCache && cur->Cache.Level == 3) {
                DWORD_PTR mask = cur->Cache.GroupMask.Mask & processAffinity;
                if (mask != 0) {
                    l3Masks[l3Count++] = mask;
                }
            }
            ptr += cur->Size;
        }
    }

    // Find the primary L3 cache containing the most performance cores
    DWORD_PTR primaryL3Mask = 0;
    if (l3Count > 0) {
        int bestCoreCount = -1;
        int bestL3Idx = 0;
        for (int i = 0; i < l3Count; ++i) {
            int count = 0;
            BYTE* ptr = buffer.data();
            BYTE* end = ptr + returnLength;
            while (ptr < end) {
                auto cur = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(ptr);
                if (cur->Relationship == RelationProcessorCore && cur->Processor.EfficiencyClass == maxEfficiency) {
                    if (cur->Processor.GroupCount > 0 && (cur->Processor.GroupMask[0].Mask & l3Masks[i]) != 0) {
                        count++;
                    }
                }
                ptr += cur->Size;
            }
            if (count > bestCoreCount) {
                bestCoreCount = count;
                bestL3Idx = i;
            }
        }
        if (bestCoreCount > 0) {
            primaryL3Mask = l3Masks[bestL3Idx];
        }
    }

    DWORD_PTR pCoreMasks[64] = {};
    int pCoreCount = 0;
    bool core0Found = false;
    DWORD_PTR core0Mask = 0;

    auto collectCores = [&](bool inPrimaryL3) {
        BYTE* ptr = buffer.data();
        BYTE* end = ptr + returnLength;
        while (ptr < end && pCoreCount < 64) {
            auto cur = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(ptr);
            if (cur->Relationship == RelationProcessorCore && cur->Processor.GroupCount > 0) {
                DWORD_PTR mask = cur->Processor.GroupMask[0].Mask & processAffinity;
                if (mask != 0) {
                    if (!core0Found) { core0Mask = mask; core0Found = true; }
                    if (cur->Processor.EfficiencyClass == maxEfficiency) {
                        bool matches = (primaryL3Mask == 0) || ((mask & primaryL3Mask) != 0);
                        if (matches == inPrimaryL3) {
                            bool already = false;
                            for (int k = 0; k < pCoreCount; ++k) {
                                if (pCoreMasks[k] == mask) { already = true; break; }
                            }
                            if (!already) {
                                pCoreMasks[pCoreCount++] = mask;
                            }
                        }
                    }
                }
            }
            ptr += cur->Size;
        }
    };

    if (primaryL3Mask != 0) {
        collectCores(true);
        collectCores(false);
    } else {
        collectCores(false);
    }

    auto maskToCore = [](DWORD_PTR mask) noexcept -> std::pair<DWORD_PTR, DWORD> {
        unsigned long idx = 0;
        if (_BitScanForward64(&idx, mask)) return { 1ULL << idx, static_cast<DWORD>(idx) };
        return { 0, 0 };
    };

    if (pCoreCount >= 1) {
        int targetDisp = (pCoreCount >= 4) ? 2 : ((pCoreCount >= 3) ? 1 : ((pCoreCount > 1) ? 1 : 0));
        int targetReader = (pCoreCount >= 4) ? 3 : ((pCoreCount >= 3) ? 2 : (pCoreCount - 1));

        DWORD_PTR rRaw = pCoreMasks[targetReader];
        DWORD_PTR dRaw = pCoreMasks[targetDisp];

        if (pCoreCount > 1) {
            if ((rRaw & ~1ULL) != 0) rRaw &= ~1ULL;
            if ((dRaw & ~1ULL) != 0) dRaw &= ~1ULL;
        }

        auto [rMask, rCore] = maskToCore(rRaw);
        auto [dMask, dCore] = maskToCore(dRaw);
        result.reader = rMask;
        result.readerCore = rCore;
        result.dispatcher = dMask;
        result.dispatcherCore = dCore;
        if (pCoreCount >= 3 && core0Mask) {
            auto [uMask, _] = maskToCore(core0Mask);
            result.ui = uMask;
        } else if (pCoreCount >= 3) {
            result.ui = rMask;
        } else {
            auto [u0Mask, _] = maskToCore(pCoreMasks[0]);
            result.ui = u0Mask;
        }
        return result;
    }

    auto [defMask, defCore] = maskToCore(processAffinity);
    result.reader = defMask ? defMask : processAffinity;
    result.readerCore = defCore;
    result.dispatcher = result.reader;
    result.dispatcherCore = result.readerCore;
    result.ui = result.reader;
    return result;
}

std::string GetConflictingDriverProcess() {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return "";
    PROCESSENTRY32W pe{ sizeof(PROCESSENTRY32W) };
    if (Process32FirstW(hSnap, &pe)) {
        do {
            wchar_t lowName[MAX_PATH] = {};
            for (int i = 0; pe.szExeFile[i] && i < MAX_PATH - 1; ++i) {
                wchar_t c = pe.szExeFile[i];
                lowName[i] = (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + 32) : c;
            }
            if (wcsstr(lowName, L"opentabletdriver") != nullptr ||
                wcsstr(lowName, L"pentablet") != nullptr ||
                wcsstr(lowName, L"tabletdriver") != nullptr ||
                wcsstr(lowName, L"vmulti") != nullptr ||
                (wcsstr(lowName, L"osupoint") != nullptr && pe.th32ProcessID != GetCurrentProcessId())) {
                CloseHandle(hSnap);
                char mbName[MAX_PATH] = {};
                WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, mbName, sizeof(mbName), nullptr, nullptr);
                return mbName;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return "";
}

COLORREF g_colorAccent = RGB(255, 102, 170);
COLORREF g_colorAccentHover = RGB(255, 130, 185);
COLORREF g_colorAccentPressed = RGB(230, 80, 150);
COLORREF g_colorAccentSubtle = RGB(70, 44, 60);

HBRUSH g_hBrushAccent = NULL;
HBRUSH g_hBrushAccentSubtle = NULL;
HPEN   g_hPenAccent = NULL;
HBRUSH g_hBrushText = NULL;
HPEN   g_hPenAccentThin = NULL;
HBRUSH g_hBrushPreviewBezel = NULL;
HBRUSH g_hBrushPreviewActive = NULL;

COLORREF GetWindowsSystemAccentColor() noexcept {
    auto boostLuminance = [](BYTE r, BYTE g, BYTE b) noexcept -> COLORREF {
        double lum = 0.2126 * r + 0.7152 * g + 0.0722 * b;
        if (lum < 95.0) {
            double boost = 95.0 / (lum > 10.0 ? lum : 10.0);
            r = static_cast<BYTE>((std::min)(255.0, r * boost));
            g = static_cast<BYTE>((std::min)(255.0, g * boost));
            b = static_cast<BYTE>((std::min)(255.0, b * boost));
        }
        return RGB(r, g, b);
    };

    DWORD accent = 0;
    DWORD size = sizeof(accent);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor",
                     RRF_RT_REG_DWORD, nullptr, &accent, &size) == ERROR_SUCCESS) {
        BYTE r = static_cast<BYTE>(accent & 0xFF);
        BYTE g = static_cast<BYTE>((accent >> 8) & 0xFF);
        BYTE b = static_cast<BYTE>((accent >> 16) & 0xFF);
        return boostLuminance(r, g, b);
    }

    DWORD colorization = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&colorization, &opaque))) {
        BYTE r = static_cast<BYTE>((colorization >> 16) & 0xFF);
        BYTE g = static_cast<BYTE>((colorization >> 8) & 0xFF);
        BYTE b = static_cast<BYTE>(colorization & 0xFF);
        return boostLuminance(r, g, b);
    }

    return RGB(255, 102, 170);
}

ThemeTokens g_theme{};
bool g_isLightTheme = false;

HBRUSH g_hBrushBg = NULL;
HBRUSH g_hBrushCard = NULL;
HBRUSH g_hBrushEdit = NULL;
HPEN   g_hPenCardBorder = NULL;
HPEN   g_hPenEditBorder = NULL;

extern HWND g_hwnd;
extern bool g_backingDirty;

bool IsWindowsAppsLightTheme() noexcept {
    DWORD val = 0;
    DWORD valSize = sizeof(val);
    LSTATUS status = RegGetValueA(
        HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        "AppsUseLightTheme",
        RRF_RT_REG_DWORD,
        NULL,
        &val,
        &valSize
    );
    return (status == ERROR_SUCCESS && val == 1);
}

void UpdateSystemAccentPalette() noexcept {
    if (_stricmp(g_activePalette.c_str(), "light") == 0 || _stricmp(g_activePalette.c_str(), "white") == 0) {
        g_isLightTheme = true;
    }
    else if (_stricmp(g_activePalette.c_str(), "dark") == 0) {
        g_isLightTheme = false;
    }
    else if (_stricmp(g_activePalette.c_str(), "system") == 0 || g_activePalette.empty()) {
        g_isLightTheme = IsWindowsAppsLightTheme();
    }
    else {
        g_isLightTheme = false;
    }

    if (g_isLightTheme) {
        g_theme.bg            = RGB(243, 243, 248);
        g_theme.cardBg        = RGB(255, 255, 255);
        g_theme.cardBorder    = RGB(222, 222, 230);
        g_theme.editBg        = RGB(248, 248, 252);
        g_theme.border        = RGB(205, 205, 215);
        g_theme.text          = RGB(15, 15, 20);
        g_theme.textMuted     = RGB(75, 75, 90);
        g_theme.btnBg         = RGB(245, 245, 250);
        g_theme.btnHover      = RGB(232, 232, 240);
        g_theme.previewBezel  = RGB(230, 230, 236);
        g_theme.previewActive = RGB(244, 244, 248);
        g_theme.previewGrid   = RGB(218, 218, 226);
    }
    else {
        g_theme.bg            = RGB(14, 14, 17);
        g_theme.cardBg        = RGB(22, 22, 26);
        g_theme.cardBorder    = RGB(40, 40, 48);
        g_theme.editBg        = RGB(30, 30, 36);
        g_theme.border        = RGB(52, 52, 62);
        g_theme.text          = RGB(245, 245, 250);
        g_theme.textMuted     = RGB(185, 185, 200);
        g_theme.btnBg         = RGB(30, 30, 36);
        g_theme.btnHover      = RGB(42, 42, 52);
        g_theme.previewBezel  = RGB(18, 18, 22);
        g_theme.previewActive = RGB(12, 12, 16);
        g_theme.previewGrid   = RGB(28, 28, 36);
    }

    COLORREF chosenColor = 0;
    bool foundCustom = false;
    if (!g_activePalette.empty() && 
        g_activePalette != "system" && 
        g_activePalette != "light" && 
        g_activePalette != "dark" &&
        g_activePalette != "white") {
        for (size_t i = 0; i < g_numPalettes; ++i) {
            if (_stricmp(g_availablePalettes[i].id, g_activePalette.c_str()) == 0 ||
                _stricmp(g_availablePalettes[i].name, g_activePalette.c_str()) == 0) {
                chosenColor = g_availablePalettes[i].color;
                foundCustom = true;
                break;
            }
        }
        if (!foundCustom) {
            for (const auto& cp : g_customPalettes) {
                if (_stricmp(cp.name.c_str(), g_activePalette.c_str()) == 0) {
                    chosenColor = cp.color;
                    foundCustom = true;
                    break;
                }
            }
        }
    }
    if (!foundCustom) {
        chosenColor = GetWindowsSystemAccentColor();
    }
    g_colorAccent = chosenColor;

    BYTE r = GetRValue(g_colorAccent);
    BYTE g = GetGValue(g_colorAccent);
    BYTE b = GetBValue(g_colorAccent);

    // Hover (+18% brightness)
    BYTE hr = static_cast<BYTE>((std::min)(255, static_cast<int>(r + (255 - r) * 0.18)));
    BYTE hg = static_cast<BYTE>((std::min)(255, static_cast<int>(g + (255 - g) * 0.18)));
    BYTE hb = static_cast<BYTE>((std::min)(255, static_cast<int>(b + (255 - b) * 0.18)));
    g_colorAccentHover = RGB(hr, hg, hb);

    // Pressed (-18% brightness)
    BYTE pr = static_cast<BYTE>((std::max)(0, static_cast<int>(r * 0.82)));
    BYTE pg = static_cast<BYTE>((std::max)(0, static_cast<int>(g * 0.82)));
    BYTE pb = static_cast<BYTE>((std::max)(0, static_cast<int>(b * 0.82)));
    g_colorAccentPressed = RGB(pr, pg, pb);

    // Subtle area fill
    BYTE sr, sg, sb;
    if (g_isLightTheme) {
        sr = static_cast<BYTE>(255 - (255 - r) * 0.22);
        sg = static_cast<BYTE>(255 - (255 - g) * 0.22);
        sb = static_cast<BYTE>(255 - (255 - b) * 0.22);
    } else {
        sr = static_cast<BYTE>(22 + (r - 22) * 0.22);
        sg = static_cast<BYTE>(22 + (g - 22) * 0.22);
        sb = static_cast<BYTE>(26 + (b - 26) * 0.22);
    }
    g_colorAccentSubtle = RGB(sr, sg, sb);

    DeleteGdiObjects(
        g_hBrushBg, g_hBrushCard, g_hBrushEdit, g_hPenCardBorder, g_hPenEditBorder,
        g_hBrushAccent, g_hBrushAccentSubtle, g_hPenAccent, g_hBrushText,
        g_hPenAccentThin, g_hBrushPreviewBezel, g_hBrushPreviewActive
    );

    g_hBrushBg = CreateSolidBrush(g_theme.bg);
    g_hBrushCard = CreateSolidBrush(g_theme.cardBg);
    g_hBrushEdit = CreateSolidBrush(g_theme.editBg);
    g_hPenCardBorder = CreatePen(PS_SOLID, 1, g_theme.cardBorder);
    g_hPenEditBorder = CreatePen(PS_SOLID, 1, g_theme.border);
    g_hBrushAccent = CreateSolidBrush(g_colorAccent);
    g_hBrushAccentSubtle = CreateSolidBrush(g_colorAccentSubtle);
    g_hPenAccent = CreatePen(PS_SOLID, 2, g_colorAccent);
    g_hBrushText = CreateSolidBrush(g_theme.text);
    g_hPenAccentThin = CreatePen(PS_SOLID, 1, g_colorAccent);
    g_hBrushPreviewBezel = CreateSolidBrush(g_theme.previewBezel);
    g_hBrushPreviewActive = CreateSolidBrush(g_theme.previewActive);

    if (g_hwnd) {
        BOOL darkMode = !g_isLightTheme;
        DwmSetWindowAttribute(g_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
    }
    g_backingDirty = true;
}

static bool s_consoleAllocated = false;
static std::atomic_flag s_consoleLock = ATOMIC_FLAG_INIT;

void ConsoleLock() noexcept {
    while (s_consoleLock.test_and_set(std::memory_order_acquire)) {
        _mm_pause();
    }
}

void ConsoleUnlock() noexcept {
    s_consoleLock.clear(std::memory_order_release);
}

static int CountVisibleColumns(const char* s) noexcept {
    int cols = 0;
    const char* p = s;
    while (*p) {
        if (*p == '\x1b' && p[1] == '[') {
            p += 2;
            while (*p && !(*p >= '@' && *p <= '~')) ++p;
            if (*p) ++p;
            continue;
        }
        unsigned char uc = static_cast<unsigned char>(*p);
        if ((uc & 0xC0) != 0x80) {
            cols++;
        }
        ++p;
    }
    return cols;
}

static int AppendClippedRow(char* dst, size_t dstCap, int dstLen, int row, int maxCols, const char* text) noexcept {
    if (dstLen < 0 || static_cast<size_t>(dstLen) + 32 >= dstCap) return dstLen;
    int n = snprintf(dst + dstLen, dstCap - static_cast<size_t>(dstLen), "\x1b[%d;1H\x1b[2K", row);
    if (n > 0) dstLen += n;

    int visCols = 0;
    bool hasAnsi = false;
    const char* p = text;
    while (*p && static_cast<size_t>(dstLen) + 12 < dstCap) {
        if (*p == '\x1b' && p[1] == '[') {
            hasAnsi = true;
            const char* seqStart = p;
            p += 2;
            while (*p && !(*p >= '@' && *p <= '~')) ++p;
            if (*p) ++p;
            size_t seqLen = static_cast<size_t>(p - seqStart);
            if (static_cast<size_t>(dstLen) + seqLen + 8 < dstCap) {
                std::memcpy(dst + dstLen, seqStart, seqLen);
                dstLen += static_cast<int>(seqLen);
            }
            continue;
        }

        unsigned char uc = static_cast<unsigned char>(*p);
        int charBytes = 1;
        if ((uc & 0xE0) == 0xC0) charBytes = 2;
        else if ((uc & 0xF0) == 0xE0) charBytes = 3;
        else if ((uc & 0xF8) == 0xF0) charBytes = 4;

        if (visCols + 1 > maxCols) break;
        if (static_cast<size_t>(dstLen) + static_cast<size_t>(charBytes) + 8 >= dstCap) break;

        for (int i = 0; i < charBytes && *p; ++i) {
            dst[dstLen++] = *p++;
        }
        visCols++;
    }

    if (hasAnsi && static_cast<size_t>(dstLen) + 6 < dstCap) {
        std::memcpy(dst + dstLen, "\x1b[0m", 4);
        dstLen += 4;
    }
    if (static_cast<size_t>(dstLen) + 2 < dstCap) {
        dst[dstLen++] = '\n';
        dst[dstLen] = '\0';
    }
    return dstLen;
}

void UpdatePinnedTelemetry(const ConsoleTelemetry& t) noexcept {
    if (!g_debugMode) return;
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!hOut || hOut == INVALID_HANDLE_VALUE) return;

    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    int cols = 120;
    if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
        int winCols = static_cast<int>(csbi.srWindow.Right - csbi.srWindow.Left + 1);
        cols = (winCols > 0) ? winCols : (csbi.dwSize.X > 0 ? static_cast<int>(csbi.dwSize.X) : 120);
    }
    cols = (std::clamp)(cols, 60, 240);
    const int maxCols = cols - 1;

    char buf[4096];
    int len = 0;
    len += snprintf(buf + len, sizeof(buf) - len, "\x1b[s");

    char line[512];

    if (t.connected) {
        len = AppendClippedRow(buf, sizeof(buf), len, 1, maxCols, "\x1b[1;36mosu!Point Telemetry & Diagnostics\x1b[0m");

        snprintf(line, sizeof(line), "Device:   %s (0x%04X:0x%04X) | Phys: %.1f x %.1f mm | Max: %u x %u | MaxP: %u",
            t.deviceName.c_str(), t.vid, t.pid, t.phys_w, t.phys_h, t.max_raw_x, t.max_raw_y, t.max_pressure);
        if (CountVisibleColumns(line) > maxCols) {
            snprintf(line, sizeof(line), "Device:   %s (%04X:%04X) | Phys:%.0fx%.0fmm | Max:%ux%u | P:%u",
                t.deviceName.c_str(), t.vid, t.pid, t.phys_w, t.phys_h, t.max_raw_x, t.max_raw_y, t.max_pressure);
        }
        len = AppendClippedRow(buf, sizeof(buf), len, 2, maxCols, line);

        snprintf(line, sizeof(line), "Display:  Monitor #%d (%d x %d) at (%d, %d) | Virtual Desktop: %d x %d",
            t.monIdx + 1, t.monW, t.monH, t.monX, t.monY, t.deskW, t.deskH);
        if (CountVisibleColumns(line) > maxCols) {
            snprintf(line, sizeof(line), "Display:  Mon #%d (%dx%d @ %d,%d) | Desk: %dx%d",
                t.monIdx + 1, t.monW, t.monH, t.monX, t.monY, t.deskW, t.deskH);
        }
        len = AppendClippedRow(buf, sizeof(buf), len, 3, maxCols, line);

        snprintf(line, sizeof(line), "Area:     %.1f x %.1f mm at (%.1f, %.1f) | Rot: %.1f deg | Ratio: %s | Clip: %d | Lock: %d",
            t.areaW, t.areaH, t.areaCX, t.areaCY, t.areaRot, t.areaRatio.c_str(), t.clipArea ? 1 : 0, t.lockDrag ? 1 : 0);
        if (CountVisibleColumns(line) > maxCols) {
            snprintf(line, sizeof(line), "Area:     %.1fx%.1fmm (%.1f,%.1f) Rot:%.1f Ratio:%s C:%d L:%d",
                t.areaW, t.areaH, t.areaCX, t.areaCY, t.areaRot, t.areaRatio.c_str(), t.clipArea ? 1 : 0, t.lockDrag ? 1 : 0);
        }
        len = AppendClippedRow(buf, sizeof(buf), len, 4, maxCols, line);

        snprintf(line, sizeof(line), "Live Pen: Raw=(%5d, %5d) -> Norm=(%.3f, %.3f) -> Screen=(%4d, %4d) px [SendInput: %5d, %5d] | Range: %s",
            t.penRawX, t.penRawY, t.penUn, t.penVn, t.penPx, t.penPy, t.penSiX, t.penSiY, t.penInRange ? "IN" : "OUT");
        if (CountVisibleColumns(line) > maxCols) {
            snprintf(line, sizeof(line), "Live Pen: Raw(%d,%d)->Norm(%.2f,%.2f)->Scr(%d,%d)[SI:%d,%d] | %s",
                t.penRawX, t.penRawY, t.penUn, t.penVn, t.penPx, t.penPy, t.penSiX, t.penSiY, t.penInRange ? "IN" : "OUT");
        }
        len = AppendClippedRow(buf, sizeof(buf), len, 5, maxCols, line);

        snprintf(line, sizeof(line), "Pen State:Pressure: %4u / %u | Tip: %s | Barrel: %s | Extrap: %s (%.1f ms)",
            t.penPressure, t.max_pressure, t.penTip ? "DOWN" : "UP", t.penBarrel ? "DOWN" : "UP",
            t.extrapEnabled ? "ON" : "OFF", t.extrapTau);
        if (CountVisibleColumns(line) > maxCols) {
            snprintf(line, sizeof(line), "Pen State:P:%u/%u | Tip:%s | Brl:%s | Extrap:%s(%.1fms)",
                t.penPressure, t.max_pressure, t.penTip ? "DOWN" : "UP", t.penBarrel ? "DOWN" : "UP",
                t.extrapEnabled ? "ON" : "OFF", t.extrapTau);
        }
        len = AppendClippedRow(buf, sizeof(buf), len, 6, maxCols, line);

        if (t.historyCount > 0) {
            const size_t cnt = (std::min)(t.historyCount, static_cast<size_t>(4));
            char ptFull[4][48]{};
            int ptLen[4]{};
            int totalFullLen = 11; // "Trajectory:"
            for (size_t i = 0; i < cnt; ++i) {
                ptLen[i] = snprintf(ptFull[i], sizeof(ptFull[i]), " [%zu](%d,%d)->(%d,%d)",
                    i + 1, t.history[i].rx, t.history[i].ry, t.history[i].px, t.history[i].py);
                totalFullLen += ptLen[i];
            }

            if (totalFullLen <= maxCols) {
                int pos = snprintf(line, sizeof(line), "Trajectory:");
                for (size_t i = 0; i < cnt; ++i) {
                    pos += snprintf(line + pos, sizeof(line) - static_cast<size_t>(pos), "%s", ptFull[i]);
                }
            } else {
                // Prioritize the newest samples ([cnt] backwards) so the latest point is never clipped
                size_t startIdx = cnt - 1;
                int accLen = 14 + ptLen[cnt - 1]; // "Trajectory: .." + newest sample
                while (startIdx > 0) {
                    if (accLen + ptLen[startIdx - 1] <= maxCols) {
                        accLen += ptLen[startIdx - 1];
                        startIdx--;
                    } else {
                        break;
                    }
                }
                if (accLen <= maxCols) {
                    int pos = snprintf(line, sizeof(line), "Trajectory: ..");
                    for (size_t i = startIdx; i < cnt; ++i) {
                        pos += snprintf(line + pos, sizeof(line) - static_cast<size_t>(pos), "%s", ptFull[i]);
                    }
                } else {
                    // Ultra-narrow fallback (< 85 cols): compact raw-only for previous point + full newest point
                    if (cnt >= 2) {
                        snprintf(line, sizeof(line), "Trajectory: .. [%zu](%d,%d)%s",
                            cnt - 1, t.history[cnt - 2].rx, t.history[cnt - 2].ry, ptFull[cnt - 1]);
                        if (CountVisibleColumns(line) > maxCols) {
                            snprintf(line, sizeof(line), "Trajectory:%s", ptFull[cnt - 1]);
                        }
                    } else {
                        snprintf(line, sizeof(line), "Trajectory:%s", ptFull[0]);
                    }
                }
            }
        } else {
            snprintf(line, sizeof(line), "Trajectory: [Waiting for movement...]");
        }
        len = AppendClippedRow(buf, sizeof(buf), len, 7, maxCols, line);

        snprintf(line, sizeof(line), "Pipeline: Rate: %.0f Hz (EP%d) | Packets: %.1fk ok | %llu rej | %.0f%% dedup",
            t.hz, t.ep, t.okCount / 1000.0, static_cast<unsigned long long>(t.rejCount), t.dedupPct);
        if (CountVisibleColumns(line) > maxCols) {
            snprintf(line, sizeof(line), "Pipeline: %.0fHz(EP%d) | ok:%.1fk | rej:%llu | dedup:%.0f%%",
                t.hz, t.ep, t.okCount / 1000.0, static_cast<unsigned long long>(t.rejCount), t.dedupPct);
        }
        len = AppendClippedRow(buf, sizeof(buf), len, 8, maxCols, line);

        snprintf(line, sizeof(line), "Timing:   lat p50=%uus p99=%uus max=%uus | jit: \xC2\xB1%uus | Last Err: %lu | Conn: %d",
            t.lat_p50, t.lat_p99, t.lat_max, t.jit, t.lastErr, t.connCount);
        if (CountVisibleColumns(line) > maxCols) {
            snprintf(line, sizeof(line), "Timing:   lat %u/%u/%uus | jit:\xC2\xB1%uus | Err:%lu | Conn:%d",
                t.lat_p50, t.lat_p99, t.lat_max, t.jit, t.lastErr, t.connCount);
        }
        len = AppendClippedRow(buf, sizeof(buf), len, 9, maxCols, line);
    } else {
        len = AppendClippedRow(buf, sizeof(buf), len, 1, maxCols, "\x1b[1;33mosu!Point Telemetry & Diagnostics - WAITING\x1b[0m");
        len = AppendClippedRow(buf, sizeof(buf), len, 2, maxCols, "Device:   [Waiting for tablet connection...]");

        snprintf(line, sizeof(line), "Display:  Monitor #%d (%d x %d) | Desktop: %d x %d",
            t.monIdx + 1, t.monW, t.monH, t.deskW, t.deskH);
        len = AppendClippedRow(buf, sizeof(buf), len, 3, maxCols, line);

        snprintf(line, sizeof(line), "Area:     %.1f x %.1f mm at (%.1f, %.1f)",
            t.areaW, t.areaH, t.areaCX, t.areaCY);
        len = AppendClippedRow(buf, sizeof(buf), len, 4, maxCols, line);

        len = AppendClippedRow(buf, sizeof(buf), len, 5, maxCols, "Live Pen: [Pen not in range]");
        len = AppendClippedRow(buf, sizeof(buf), len, 6, maxCols, "Pen State:Pressure: 0 | Tip: UP | Barrel: UP");
        len = AppendClippedRow(buf, sizeof(buf), len, 7, maxCols, "Trajectory: -");
        len = AppendClippedRow(buf, sizeof(buf), len, 8, maxCols, "Pipeline: Rate: 0 Hz | ok: 0 | rej: 0");
        len = AppendClippedRow(buf, sizeof(buf), len, 9, maxCols, "Timing:   lat: - | jit: -");
    }

    const int sepLen = (std::min)(maxCols, 120);
    char sepLine[128];
    std::memset(sepLine, '-', static_cast<size_t>(sepLen));
    sepLine[sepLen] = '\0';
    len = AppendClippedRow(buf, sizeof(buf), len, 10, maxCols, sepLine);

    len = AppendClippedRow(buf, sizeof(buf), len, 11, maxCols, "System events and diagnostics log (Scroll area below):");

    std::memset(sepLine, '=', static_cast<size_t>(sepLen));
    sepLine[sepLen] = '\0';
    len = AppendClippedRow(buf, sizeof(buf), len, 12, maxCols, sepLine);

    if (static_cast<size_t>(len) + 4 < sizeof(buf)) {
        len += snprintf(buf + len, sizeof(buf) - static_cast<size_t>(len), "\x1b[u");
    }

    ConsoleLock();
    fwrite(buf, 1, static_cast<size_t>(len), stdout);
    fflush(stdout);
    ConsoleUnlock();
}

void InitPinnedConsoleHeader() noexcept {
    if (!g_debugMode) return;
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!hOut || hOut == INVALID_HANDLE_VALUE) return;

    ConsoleLock();
    printf("\x1b[2J\x1b[13;r\x1b[13;1H");
    fflush(stdout);
    ConsoleUnlock();

    ConsoleTelemetry t{};
    if (!g_spec.name.empty() && g_spec.vid != 0) {
        t.connected = true;
        t.deviceName = g_spec.name;
        t.vid = g_spec.vid;
        t.pid = g_spec.pid;
        t.phys_w = g_spec.phys_w;
        t.phys_h = g_spec.phys_h;
    }
    UpdatePinnedTelemetry(t);
}

void SetupDebugConsole(bool enable) noexcept {
    HWND hCon = GetConsoleWindow();
    if (!enable) {
        if (hCon && s_consoleAllocated) {
            ShowWindow(hCon, SW_HIDE);
        }
        printf("\x1b[r"); // Restore full scrolling margins
        fflush(stdout);
        return;
    }

    if (!hCon) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
            if (AllocConsole()) {
                s_consoleAllocated = true;
            }
        }
        hCon = GetConsoleWindow();
    }

    if (hCon) {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD type = (hOut != INVALID_HANDLE_VALUE && hOut != nullptr) ? GetFileType(hOut) : FILE_TYPE_UNKNOWN;
        if (type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK) {
            int fd = _open_osfhandle(reinterpret_cast<intptr_t>(hOut), _O_TEXT);
            if (fd >= 0) {
                FILE* fp = _fdopen(fd, "w");
                if (fp) {
                    *stdout = *fp;
                    setvbuf(stdout, NULL, _IONBF, 0);
                }
            }
        } else {
            FILE* dummy = nullptr;
            freopen_s(&dummy, "CONOUT$", "w", stdout);
            freopen_s(&dummy, "CONOUT$", "w", stderr);
            freopen_s(&dummy, "CONIN$", "r", stdin);
            setvbuf(stdout, NULL, _IONBF, 0);
            setvbuf(stderr, NULL, _IONBF, 0);
        }

        HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
        DWORD prev_mode = 0;
        if (GetConsoleMode(hInput, &prev_mode)) {
            SetConsoleMode(hInput, prev_mode & ~ENABLE_QUICK_EDIT_MODE);
        }

        if (hOut != INVALID_HANDLE_VALUE && hOut != nullptr) {
            DWORD dwMode = 0;
            if (GetConsoleMode(hOut, &dwMode)) {
                dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                SetConsoleMode(hOut, dwMode);
            }
            if (s_consoleAllocated) {
                COORD largest = GetLargestConsoleWindowSize(hOut);
                SHORT targetCols = (largest.X > 0) ? (std::min)(static_cast<SHORT>(140), largest.X) : static_cast<SHORT>(140);
                SHORT targetRows = (largest.Y > 0) ? (std::min)(static_cast<SHORT>(36), largest.Y) : static_cast<SHORT>(36);
                CONSOLE_SCREEN_BUFFER_INFO csbi{};
                if (GetConsoleScreenBufferInfo(hOut, &csbi)) {
                    SHORT curWinW = static_cast<SHORT>(csbi.srWindow.Right - csbi.srWindow.Left + 1);
                    SHORT curWinH = static_cast<SHORT>(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
                    COORD tempBuf = {
                        (std::max)(csbi.dwSize.X, targetCols),
                        (std::max)(csbi.dwSize.Y, static_cast<SHORT>(2000))
                    };
                    SetConsoleScreenBufferSize(hOut, tempBuf);
                    if (curWinW < targetCols || curWinH < targetRows) {
                        SMALL_RECT winRect = { 0, 0, static_cast<SHORT>(targetCols - 1), static_cast<SHORT>(targetRows - 1) };
                        SetConsoleWindowInfo(hOut, TRUE, &winRect);
                    }
                    COORD finalBuf = { targetCols, static_cast<SHORT>(2000) };
                    SetConsoleScreenBufferSize(hOut, finalBuf);
                }
            }
        }

        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);

        if (s_consoleAllocated) {
            HMENU hSysMenu = GetSystemMenu(hCon, FALSE);
            if (hSysMenu) DeleteMenu(hSysMenu, SC_CLOSE, MF_BYCOMMAND);
        }

        SetConsoleTitleA("osu!Point - Debug Console");
        ShowWindow(hCon, SW_SHOW);

        InitPinnedConsoleHeader();
    }
}

