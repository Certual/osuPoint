#pragma once
#define _CRT_SECURE_NO_WARNINGS
#pragma warning(disable: 4324)
#pragma warning(disable: 4189)
#pragma warning(disable: 4100)
#pragma warning(disable: 4244)
#pragma warning(disable: 4018)

#ifndef _HAS_EXCEPTIONS
#define _HAS_EXCEPTIONS 0
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
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
#include <intrin.h>

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
#include <chrono>
#include <tlhelp32.h>

#pragma warning(push)
#pragma warning(disable: 4458)
#include <unknwn.h>
#include <objidl.h>
#include <commdlg.h>
#include <gdiplus.h>
#pragma warning(pop)

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "synchronization.lib")


constexpr double PI = 3.14159265358979323846;
constexpr UINT WM_TRAYICON = WM_USER + 1;
constexpr UINT IDM_TRAY_OPEN = 2001;
constexpr UINT IDM_TRAY_EXIT = 2002;
constexpr UINT IDM_TRAY_PENCLICK = 2003;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// Fluent UI 2 theme color tokens - Dynamic Light / Dark System Support
struct ThemeTokens {
    COLORREF bg;
    COLORREF cardBg;
    COLORREF cardBorder;
    COLORREF editBg;
    COLORREF border;
    COLORREF text;
    COLORREF textMuted;
    COLORREF btnBg;
    COLORREF btnHover;
    COLORREF previewBezel;
    COLORREF previewActive;
    COLORREF previewGrid;
};

extern ThemeTokens g_theme;
extern bool g_isLightTheme;

#define COLOR_BG          (g_theme.bg)
#define COLOR_CARD_BG     (g_theme.cardBg)
#define COLOR_CARD_BORDER (g_theme.cardBorder)
#define COLOR_EDIT_BG     (g_theme.editBg)
#define COLOR_BORDER      (g_theme.border)
#define COLOR_TEXT        (g_theme.text)
#define COLOR_TEXT_MUTED  (g_theme.textMuted)
#define COLOR_BTN         (g_theme.btnBg)
#define COLOR_BTN_HOVER   (g_theme.btnHover)

extern HBRUSH g_hBrushBg;
extern HBRUSH g_hBrushCard;
extern HBRUSH g_hBrushEdit;
extern HPEN   g_hPenCardBorder;
extern HPEN   g_hPenEditBorder;

struct ColorPaletteDef {
    const char* id;
    const char* name;
    COLORREF color;
};

inline const ColorPaletteDef g_availablePalettes[] = {
    { "system",  "System",       RGB(0, 0, 0) },
    { "light",   "Light",        RGB(0, 120, 215) },
    { "dark",    "Dark",         RGB(0, 120, 215) },
    { "pink",    "Sakura Pink",  RGB(255, 102, 170) },
    { "cyan",    "Ice Cyan",     RGB(0, 210, 255) },
    { "emerald", "Emerald",      RGB(0, 230, 118) },
    { "violet",  "Sunset Violet",RGB(168, 85, 247) },
    { "amber",   "Golden Amber", RGB(255, 179, 0) },
    { "mono",    "Monochrome",   RGB(240, 240, 245) }
};
constexpr size_t g_numPalettes = sizeof(g_availablePalettes) / sizeof(g_availablePalettes[0]);

// Dynamic Windows System Accent color & derived palette
extern COLORREF g_colorAccent;
extern COLORREF g_colorAccentHover;
extern COLORREF g_colorAccentPressed;
extern COLORREF g_colorAccentSubtle;
extern HBRUSH   g_hBrushAccent;
extern HBRUSH   g_hBrushAccentSubtle;
extern HPEN     g_hPenAccent;
extern HBRUSH   g_hBrushText;
extern HPEN     g_hPenAccentThin;
extern HBRUSH   g_hBrushPreviewBezel;
extern HBRUSH   g_hBrushPreviewActive;


extern bool g_debugMode;
extern bool g_debugCliOverride;

void ConsoleLock() noexcept;
void ConsoleUnlock() noexcept;

inline void LogMessage(const char* msg) noexcept {
    if (g_debugMode) [[unlikely]] {
        ConsoleLock();
        printf("%s\n", msg);
        fflush(stdout);
        ConsoleUnlock();
    }
}
inline void LogMessage(const std::string& msg) noexcept {
    if (g_debugMode) [[unlikely]] {
        ConsoleLock();
        printf("%s\n", msg.c_str());
        fflush(stdout);
        ConsoleUnlock();
    }
}

template <typename... Args>
inline void DeleteGdiObjects(Args&... handles) noexcept {
    auto clean = [](auto& h) {
        if (h) {
            DeleteObject(h);
            h = NULL;
        }
    };
    (clean(handles), ...);
}

__forceinline __m128d FmaddPd(__m128d a, __m128d b, __m128d c) noexcept {
#if defined(__AVX2__) || defined(__FMA__)
    return _mm_fmadd_pd(a, b, c);
#else
    return _mm_add_pd(_mm_mul_pd(a, b), c);
#endif
}
