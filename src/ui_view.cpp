#pragma code_seg(".text$cold")
#include "ui_view.h"
#include "config_manager.h"
#include "driver_pipeline.h"
#include "platform.h"

HWND g_hwnd = NULL;
bool g_guiReady = false;
bool g_isUpdatingUI = false;
bool g_backingDirty = true;

HFONT g_hFont = NULL;
HFONT g_hFontSmall = NULL;
NOTIFYICONDATAA g_nid = { 0 };
static HICON s_hCustomIcon = NULL;

HWND btnTabMain = NULL, btnTabSettings = NULL;
HWND lblMon = NULL, hCboMon = NULL;
WNDPROC g_origCboProc = NULL;
HWND lblPreset = NULL, hCboPreset = NULL, btnSavePreset = NULL, btnDelPreset = NULL;
HWND lblW = NULL, hW = NULL, lblH = NULL, hH = NULL, lblCX = NULL, hCX = NULL, lblCY = NULL, hCY = NULL, lblRot = NULL, hRot = NULL;
HWND chkRatio = NULL, hRatio = NULL, btnAutoRatio = NULL, btnArea = NULL;
HWND chkLockDrag = NULL;
HWND chkClipArea = NULL;
HWND chkPenClick = NULL;
HWND chkAntiAlias = NULL;
HWND chkDebugMode = NULL;
HWND chkExtrapolate = NULL;
HWND lblExtraTau = NULL, btnExtraDec = NULL, btnExtraVal = NULL, btnExtraInc = NULL;
HWND lblPalette = NULL, hCboPalette = NULL, btnPickPalette = NULL, btnSavePalette = NULL, btnDelPalette = NULL;
HWND btnSetBg = NULL, btnClearBg = NULL, chkCopyBgImage = NULL, chkDeleteOldBg = NULL;
HWND lblOpacity = NULL, btnOpacityDec = NULL, btnOpacityVal = NULL, btnOpacityInc = NULL;
HWND lblBlur = NULL, btnBlurDec = NULL, btnBlurVal = NULL, btnBlurInc = NULL;
HWND lblRatioPrec = NULL, btnRatioPrecDec = NULL, btnRatioPrecVal = NULL, btnRatioPrecInc = NULL;
HWND btnReset = NULL;

double g_uiScale = 1.0;

constexpr int BASE_WIDTH = 640;
constexpr int BASE_HEIGHT = 330;

constexpr int LEFT_CARD_X = 10;
constexpr int LEFT_CARD_Y = 10;
constexpr int LEFT_CARD_W = 296;
constexpr int LEFT_CARD_H = 310;

constexpr int RIGHT_CARD_X = 316;
constexpr int RIGHT_CARD_Y = 10;
constexpr int RIGHT_CARD_W = 314;
constexpr int RIGHT_CARD_H = 310;

constexpr int PREVIEW_X = 326;
constexpr int PREVIEW_Y = 20;
constexpr int PREVIEW_MAX_W = 294;
constexpr int PREVIEW_MAX_H = 290;
constexpr int BEZEL = 6;

static ULONG_PTR g_gdiplusToken = 0;
static Gdiplus::Bitmap* s_pOriginalBgBitmap = nullptr;
static Gdiplus::Bitmap* s_pBgBitmap = nullptr;
static HDC s_hBackingDC = NULL;
static HBITMAP s_hBackingBm = NULL;
static HBITMAP s_hBackingOldBm = NULL;
static int s_backingW = 0;
static int s_backingH = 0;

static HDC s_hMemDC = NULL;
static HBITMAP s_hMemBm = NULL;
static HBITMAP s_hMemOldBm = NULL;
static int s_memW = 0;
static int s_memH = 0;

static void FreeMemBuffer() {
    if (s_hMemDC) {
        if (s_hMemOldBm) {
            SelectObject(s_hMemDC, s_hMemOldBm);
            s_hMemOldBm = NULL;
        }
        if (s_hMemBm) {
            DeleteObject(s_hMemBm);
            s_hMemBm = NULL;
        }
        DeleteDC(s_hMemDC);
        s_hMemDC = NULL;
        s_memW = 0;
        s_memH = 0;
    }
}

static void FreeBackingBuffer() {
    FreeMemBuffer();
    if (s_hBackingDC) {
        if (s_hBackingOldBm) {
            SelectObject(s_hBackingDC, s_hBackingOldBm);
            s_hBackingOldBm = NULL;
        }
        if (s_hBackingBm) {
            DeleteObject(s_hBackingBm);
            s_hBackingBm = NULL;
        }
        DeleteDC(s_hBackingDC);
        s_hBackingDC = NULL;
        s_backingW = 0;
        s_backingH = 0;
    }
}

static void RenderBackingBuffer(HWND hwnd) {
    if (!hwnd) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    if (!s_hBackingDC || w != s_backingW || h != s_backingH) {
        FreeBackingBuffer();
        HDC screenDC = GetDC(hwnd);
        s_hBackingDC = CreateCompatibleDC(screenDC);
        s_hBackingBm = CreateCompatibleBitmap(screenDC, w, h);
        s_hBackingOldBm = static_cast<HBITMAP>(SelectObject(s_hBackingDC, s_hBackingBm));
        s_backingW = w;
        s_backingH = h;
        ReleaseDC(hwnd, screenDC);
        g_backingDirty = true;
    }

    if (!g_backingDirty) return;

    FillRect(s_hBackingDC, &rc, g_hBrushBg);

    const double sc = g_uiScale;
    auto S = [sc](int v) noexcept -> int {
        return static_cast<int>(std::round(v * sc));
    };
    int cardRad = S(8);
    RECT leftCardRc = { S(LEFT_CARD_X), S(LEFT_CARD_Y), S(LEFT_CARD_X + LEFT_CARD_W), S(LEFT_CARD_Y + LEFT_CARD_H) };
    RECT rightCardRc = { S(RIGHT_CARD_X), S(RIGHT_CARD_Y), S(RIGHT_CARD_X + RIGHT_CARD_W), S(RIGHT_CARD_Y + RIGHT_CARD_H) };

    if (s_pBgBitmap) {
        Gdiplus::Graphics g(s_hBackingDC);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

        const float imgW = static_cast<float>(s_pBgBitmap->GetWidth());
        const float imgH = static_cast<float>(s_pBgBitmap->GetHeight());
        if (imgW > 0.0f && imgH > 0.0f) {
            float targetAspect = static_cast<float>(w) / static_cast<float>(h);
            float imgAspect = imgW / imgH;

            float srcX = 0.0f, srcY = 0.0f, srcW = imgW, srcH = imgH;
            if (imgAspect > targetAspect) {
                srcW = imgH * targetAspect;
                srcX = (imgW - srcW) * 0.5f;
            } else if (imgAspect < targetAspect) {
                srcH = imgW / targetAspect;
                srcY = (imgH - srcH) * 0.5f;
            }

            g.DrawImage(s_pBgBitmap,
                        Gdiplus::RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)),
                        srcX, srcY, srcW, srcH,
                        Gdiplus::UnitPixel);
        }

        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        BYTE alpha = (g_uiOpacity >= 100) ? 255 : static_cast<BYTE>(std::clamp(static_cast<int>(std::round(g_uiOpacity * 2.55)), 0, 255));
        COLORREF cardBgCol = g_theme.cardBg;
        Gdiplus::SolidBrush cardBrush(Gdiplus::Color(alpha, GetRValue(cardBgCol), GetGValue(cardBgCol), GetBValue(cardBgCol)));
        COLORREF cardBorderCol = g_theme.cardBorder;
        Gdiplus::Pen cardPen(Gdiplus::Color(alpha, GetRValue(cardBorderCol), GetGValue(cardBorderCol), GetBValue(cardBorderCol)), 1.0f);

        auto drawCard = [&](const RECT& cardRc) {
            float cx = static_cast<float>(cardRc.left);
            float cy = static_cast<float>(cardRc.top);
            float cw = static_cast<float>(cardRc.right - cardRc.left);
            float ch = static_cast<float>(cardRc.bottom - cardRc.top);
            float r = static_cast<float>(cardRad);
            float d = r * 2.0f;
            Gdiplus::GraphicsPath path;
            path.AddArc(cx, cy, d, d, 180.0f, 90.0f);
            path.AddArc(cx + cw - d, cy, d, d, 270.0f, 90.0f);
            path.AddArc(cx + cw - d, cy + ch - d, d, d, 0.0f, 90.0f);
            path.AddArc(cx, cy + ch - d, d, d, 90.0f, 90.0f);
            path.CloseFigure();
            g.FillPath(&cardBrush, &path);
            g.DrawPath(&cardPen, &path);
        };

        drawCard(leftCardRc);
        drawCard(rightCardRc);
    }
    else {
        HGDIOBJ oldBrush = SelectObject(s_hBackingDC, g_hBrushCard);
        HGDIOBJ oldPen = SelectObject(s_hBackingDC, g_hPenCardBorder);
        RoundRect(s_hBackingDC, leftCardRc.left, leftCardRc.top, leftCardRc.right, leftCardRc.bottom, cardRad, cardRad);
        RoundRect(s_hBackingDC, rightCardRc.left, rightCardRc.top, rightCardRc.right, rightCardRc.bottom, cardRad, cardRad);
        SelectObject(s_hBackingDC, oldPen);
        SelectObject(s_hBackingDC, oldBrush);
    }
    g_backingDirty = false;
}

static double GetBackingLuminance(int x, int y, int w, int h) {
    if (!s_hBackingDC || w <= 0 || h <= 0) {
        return g_isLightTheme ? 255.0 : 0.0;
    }
    int xs[3] = { x + w / 4, x + w / 2, x + (3 * w) / 4 };
    int ys[3] = { y + h / 4, y + h / 2, y + (3 * h) / 4 };
    double sumLum = 0.0;
    int samples = 0;
    for (int ix = 0; ix < 3; ++ix) {
        for (int iy = 0; iy < 3; ++iy) {
            COLORREF c = GetPixel(s_hBackingDC, xs[ix], ys[iy]);
            if (c != CLR_INVALID) {
                double lum = 0.299 * GetRValue(c) + 0.587 * GetGValue(c) + 0.114 * GetBValue(c);
                sumLum += lum;
                samples++;
            }
        }
    }
    return (samples > 0) ? (sumLum / samples) : (g_isLightTheme ? 255.0 : 0.0);
}

static int g_originX = 0;
static int g_originY = 0;
static double g_previewScale = 1.0;
static POINT g_previewCorners[4];
static POINT g_previewCenter;

enum class DragMode { None, Move, ResizeCorner0, ResizeCorner1, ResizeCorner2, ResizeCorner3 };
static DragMode g_dragMode = DragMode::None;
static POINT g_dragStartMouse{};
static double g_dragOrigW = 0, g_dragOrigH = 0, g_dragOrigCX = 0, g_dragOrigCY = 0;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void SetWindowTextIfChanged(HWND hCtrl, const std::string& newText) {
    if (!hCtrl) return;
    char currentText[256] = "";
    GetWindowTextA(hCtrl, currentText, sizeof(currentText));
    if (newText != currentText) {
        SetWindowTextA(hCtrl, newText.c_str());
    }
}

static void AdjustWindowRectForDpi(LPRECT prc, DWORD style, int dpi) noexcept {
    typedef BOOL(WINAPI* pfnAdjustWindowRectExForDpi)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static auto pAdjustForDpi = reinterpret_cast<pfnAdjustWindowRectExForDpi>(
        GetProcAddress(GetModuleHandleA("user32.dll"), "AdjustWindowRectExForDpi"));
    if (pAdjustForDpi) {
        pAdjustForDpi(prc, style, FALSE, 0, dpi);
    } else {
        AdjustWindowRect(prc, style, FALSE);
    }
}

static void TriggerBackingRedraw(HWND hwnd) {
    SaveConfig();
    RenderBackingBuffer(hwnd);
    RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_UPDATENOW);
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

    cx = (std::max)(0.0, (std::min)(g_spec.phys_w, cx));
    cy = (std::max)(0.0, (std::min)(g_spec.phys_h, cy));

    SetTransform(w, h, cx, cy, rot_deg, g_target_monitor_idx);

    SaveConfig();
    if (g_hwnd) {
        const double sc = g_uiScale;
        auto S = [sc](int v) noexcept -> int { return static_cast<int>(std::round(v * sc)); };
        RECT rightCardRc = { S(RIGHT_CARD_X), S(RIGHT_CARD_Y), S(RIGHT_CARD_X + RIGHT_CARD_W), S(RIGHT_CARD_Y + RIGHT_CARD_H) };
        InvalidateRect(g_hwnd, &rightCardRc, FALSE);
    }
}

void AutoMatchRatio() {
    RECT rc = GetTargetMonitorRect(g_target_monitor_idx);
    int monW = (g_disp_w > 0) ? g_disp_w : (rc.right - rc.left);
    int monH = (g_disp_h > 0) ? g_disp_h : (rc.bottom - rc.top);
    if (monW <= 0 || monH <= 0) {
        monW = 1920;
        monH = 1080;
    }

    auto gcd = [](int a, int b) {
        while (b) { int t = b; b = a % b; a = t; }
        return a;
    };
    int g = gcd(monW, monH);
    int rw = monW / g;
    int rh = monH / g;

    char buf[32];
    if (rw > 64 || rh > 64) {
        std::string fr = FormatRatio(static_cast<double>(monW) / monH, g_ratioPrecision);
        strncpy_s(buf, sizeof(buf), fr.c_str(), _TRUNCATE);
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

void AutoMatchAreaRatio() {
    double curW = ParseInput(hW);
    double curH = ParseInput(hH);
    if (curW <= 0.001 || curH <= 0.001) return;

    double r = curW / curH;
    char buf[32];

    bool isIntW = std::abs(curW - std::round(curW)) < 0.01;
    bool isIntH = std::abs(curH - std::round(curH)) < 0.01;
    bool matched = false;

    if (isIntW && isIntH) {
        static const struct { int w, h; } kStandardRatios[] = {
            { 16, 9 }, { 16, 10 }, { 4, 3 }, { 21, 9 }, { 3, 2 }, { 5, 4 }, { 1, 1 }
        };
        for (const auto& sr : kStandardRatios) {
            double stdR = static_cast<double>(sr.w) / sr.h;
            if (std::abs(r - stdR) < 0.001) {
                snprintf(buf, sizeof(buf), "%d:%d", sr.w, sr.h);
                matched = true;
                break;
            }
        }
    }

    if (!matched) {
        std::string fr = FormatRatio(r, g_ratioPrecision);
        strncpy_s(buf, sizeof(buf), fr.c_str(), _TRUNCATE);
    }

    SetWindowTextA(hRatio, buf);
    g_lockRatio = true;
    if (chkRatio) InvalidateRect(chkRatio, NULL, FALSE);
    SaveConfig();
}

void ApplyBgBlur() {
    if (s_pBgBitmap) {
        delete s_pBgBitmap;
        s_pBgBitmap = nullptr;
    }
    if (!s_pOriginalBgBitmap) {
        g_backingDirty = true;
        return;
    }

    UINT origW = s_pOriginalBgBitmap->GetWidth();
    UINT origH = s_pOriginalBgBitmap->GetHeight();
    if (origW == 0 || origH == 0) return;

    if (g_bgBlur <= 0) {
        auto* pBmp = new Gdiplus::Bitmap(origW, origH, PixelFormat32bppARGB);
        {
            Gdiplus::Graphics g(pBmp);
            g.Clear(Gdiplus::Color(255, 0, 0, 0));
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
            g.DrawImage(s_pOriginalBgBitmap, 0, 0, origW, origH);
        }
        Gdiplus::BitmapData bmpData;
        Gdiplus::Rect rect(0, 0, origW, origH);
        if (pBmp->LockBits(&rect, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bmpData) == Gdiplus::Ok) {
            auto* px = reinterpret_cast<uint32_t*>(bmpData.Scan0);
            int total = (bmpData.Stride / 4) * origH;
            for (int i = 0; i < total; ++i) {
                px[i] |= 0xFF000000u;
            }
            pBmp->UnlockBits(&bmpData);
        }
        s_pBgBitmap = pBmp;
        g_backingDirty = true;
        return;
    }

    UINT targetW = origW;
    UINT targetH = origH;
    if (targetW > 1280 || targetH > 720) {
        double scale = (std::min)(1280.0 / targetW, 720.0 / targetH);
        targetW = static_cast<UINT>(std::round(targetW * scale));
        targetH = static_cast<UINT>(std::round(targetH * scale));
    }

    auto* pBmp = new Gdiplus::Bitmap(targetW, targetH, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics g(pBmp);
        g.Clear(Gdiplus::Color(255, 0, 0, 0));
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g.DrawImage(s_pOriginalBgBitmap, 0, 0, targetW, targetH);
    }

    Gdiplus::BitmapData bmpData;
    Gdiplus::Rect rect(0, 0, targetW, targetH);
    if (pBmp->LockBits(&rect, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &bmpData) == Gdiplus::Ok) {
        int w = static_cast<int>(targetW);
        int h = static_cast<int>(targetH);
        int stride = bmpData.Stride / 4;
        auto* pixels = reinterpret_cast<uint32_t*>(bmpData.Scan0);
        std::vector<uint32_t> temp(static_cast<size_t>(w) * h);

        int radius = std::clamp(g_bgBlur, 1, 40);
        int windowSize = 2 * radius + 1;

        for (int y = 0; y < h; ++y) {
            const uint32_t* srcRow = pixels + y * stride;
            uint32_t* dstRow = temp.data() + y * w;
            int sumR = 0, sumG = 0, sumB = 0;

            for (int k = -radius; k <= radius; ++k) {
                int px = std::clamp(k, 0, w - 1);
                uint32_t col = srcRow[px];
                sumR += (col >> 16) & 0xFF;
                sumG += (col >> 8) & 0xFF;
                sumB += col & 0xFF;
            }

            for (int x = 0; x < w; ++x) {
                int r = std::clamp(sumR / windowSize, 0, 255);
                int g = std::clamp(sumG / windowSize, 0, 255);
                int b = std::clamp(sumB / windowSize, 0, 255);
                dstRow[x] = 0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);

                int leftX = std::clamp(x - radius, 0, w - 1);
                int rightX = std::clamp(x + radius + 1, 0, w - 1);
                uint32_t leftCol = srcRow[leftX];
                uint32_t rightCol = srcRow[rightX];

                sumR += static_cast<int>((rightCol >> 16) & 0xFF) - static_cast<int>((leftCol >> 16) & 0xFF);
                sumG += static_cast<int>((rightCol >> 8) & 0xFF)  - static_cast<int>((leftCol >> 8) & 0xFF);
                sumB += static_cast<int>(rightCol & 0xFF)         - static_cast<int>(leftCol & 0xFF);
            }
        }

        for (int x = 0; x < w; ++x) {
            int sumR = 0, sumG = 0, sumB = 0;

            for (int k = -radius; k <= radius; ++k) {
                int py = std::clamp(k, 0, h - 1);
                uint32_t col = temp[py * w + x];
                sumR += (col >> 16) & 0xFF;
                sumG += (col >> 8) & 0xFF;
                sumB += col & 0xFF;
            }

            for (int y = 0; y < h; ++y) {
                int r = std::clamp(sumR / windowSize, 0, 255);
                int g = std::clamp(sumG / windowSize, 0, 255);
                int b = std::clamp(sumB / windowSize, 0, 255);
                pixels[y * stride + x] = 0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);

                int topY = std::clamp(y - radius, 0, h - 1);
                int botY = std::clamp(y + radius + 1, 0, h - 1);
                uint32_t topCol = temp[topY * w + x];
                uint32_t botCol = temp[botY * w + x];

                sumR += static_cast<int>((botCol >> 16) & 0xFF) - static_cast<int>((topCol >> 16) & 0xFF);
                sumG += static_cast<int>((botCol >> 8) & 0xFF)  - static_cast<int>((topCol >> 8) & 0xFF);
                sumB += static_cast<int>(botCol & 0xFF)         - static_cast<int>(topCol & 0xFF);
            }
        }

        pBmp->UnlockBits(&bmpData);
    }

    s_pBgBitmap = pBmp;
    g_backingDirty = true;
}

void LoadBackgroundImage(const std::string& path) {
    if (s_pOriginalBgBitmap) {
        delete s_pOriginalBgBitmap;
        s_pOriginalBgBitmap = nullptr;
    }
    if (s_pBgBitmap) {
        delete s_pBgBitmap;
        s_pBgBitmap = nullptr;
    }
    if (!path.empty()) {
        std::string fullPath = path;
        if (path.find(':') == std::string::npos && path.rfind("\\\\", 0) != 0) {
            fullPath = g_baseDir + path;
        }
        std::wstring wpath = Utf8ToUtf16(fullPath);
        auto* bmp = Gdiplus::Bitmap::FromFile(wpath.c_str());
        if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) {
            s_pOriginalBgBitmap = bmp;
        } else {
            if (bmp) delete bmp;
            s_pOriginalBgBitmap = nullptr;
        }
    }
    ApplyBgBlur();
}

void UpdateBlurDisplay() {
    if (btnBlurVal) {
        char buf[16];
        if (g_bgBlur <= 0) {
            snprintf(buf, sizeof(buf), "Off");
        } else {
            snprintf(buf, sizeof(buf), "%d px", g_bgBlur);
        }
        SetWindowTextA(btnBlurVal, buf);
        InvalidateRect(btnBlurVal, NULL, FALSE);
    }
}

void RefreshPresetCombo() {
    if (!hCboPreset) return;
    SendMessage(hCboPreset, CB_RESETCONTENT, 0, 0);
    for (const auto& p : g_presets) {
        SendMessageA(hCboPreset, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(p.name.c_str()));
    }
    int idx = static_cast<int>(SendMessageA(hCboPreset, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(g_activePresetName.c_str())));
    if (idx != CB_ERR) {
        SendMessage(hCboPreset, CB_SETCURSEL, idx, 0);
    } else if (!g_presets.empty()) {
        SendMessage(hCboPreset, CB_SETCURSEL, 0, 0);
    }
}

void UpdateOpacityDisplay() {
    if (btnOpacityVal) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", g_uiOpacity);
        SetWindowTextA(btnOpacityVal, buf);
        InvalidateRect(btnOpacityVal, NULL, FALSE);
    }
    g_backingDirty = true;
}

void UpdateExtrapolateDisplay() {
    if (chkExtrapolate) {
        InvalidateRect(chkExtrapolate, NULL, FALSE);
    }
    if (btnExtraVal) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f ms", g_extrapolationTau);
        SetWindowTextA(btnExtraVal, buf);
        InvalidateRect(btnExtraVal, NULL, FALSE);
    }
}

void UpdateRatioPrecisionDisplay() {
    if (btnRatioPrecVal) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d digits", g_ratioPrecision);
        SetWindowTextA(btnRatioPrecVal, buf);
        InvalidateRect(btnRatioPrecVal, NULL, FALSE);
    }
}

void RefreshPaletteCombo() {
    if (!hCboPalette) return;
    SendMessage(hCboPalette, CB_RESETCONTENT, 0, 0);
    int selIdx = 0;
    for (size_t i = 0; i < g_numPalettes; ++i) {
        SendMessageA(hCboPalette, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(g_availablePalettes[i].name));
        if (_stricmp(g_availablePalettes[i].id, g_activePalette.c_str()) == 0 ||
            _stricmp(g_availablePalettes[i].name, g_activePalette.c_str()) == 0) {
            selIdx = static_cast<int>(i);
        }
    }
    for (size_t i = 0; i < g_customPalettes.size(); ++i) {
        SendMessageA(hCboPalette, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(g_customPalettes[i].name.c_str()));
        if (_stricmp(g_customPalettes[i].name.c_str(), g_activePalette.c_str()) == 0) {
            selIdx = static_cast<int>(g_numPalettes + i);
        }
    }
    SendMessage(hCboPalette, CB_SETCURSEL, selIdx, 0);
}

struct PromptContext {
    char title[32];
    char label[32];
    char name[64];
    bool confirmed;
    HFONT hFont;
};

static LRESULT CALLBACK PromptWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<PromptContext*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    case WM_CREATE: {
        ctx = reinterpret_cast<PromptContext*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
        CreateWindowA("STATIC", ctx ? ctx->label : "Name:", WS_VISIBLE | WS_CHILD, 18, 16, 220, 18, hwnd, NULL, NULL, NULL);
        HWND hEdit = CreateWindowA("EDIT", ctx ? ctx->name : "", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 18, 38, 224, 24, hwnd, reinterpret_cast<HMENU>(201), NULL, NULL);
        CreateWindowA("BUTTON", "Save", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 98, 72, 68, 24, hwnd, reinterpret_cast<HMENU>(IDOK), NULL, NULL);
        CreateWindowA("BUTTON", "Cancel", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 174, 72, 68, 24, hwnd, reinterpret_cast<HMENU>(IDCANCEL), NULL, NULL);
        if (ctx && ctx->hFont) {
            SendMessage(hEdit, WM_SETFONT, reinterpret_cast<WPARAM>(ctx->hFont), TRUE);
        }
        SendMessage(hEdit, EM_SETSEL, 0, -1);
        SetFocus(hEdit);
        return 0;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == IDOK) {
            if (ctx) {
                GetDlgItemTextA(hwnd, 201, ctx->name, sizeof(ctx->name));
                ctx->confirmed = true;
            }
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            if (ctx) ctx->confirmed = false;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, COLOR_TEXT);
        SetBkMode(hdc, TRANSPARENT);
        SetBkColor(hdc, COLOR_CARD_BG);
        return reinterpret_cast<LRESULT>(g_hBrushCard);
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetTextColor(hdc, COLOR_TEXT);
        SetBkColor(hdc, COLOR_EDIT_BG);
        return reinterpret_cast<LRESULT>(g_hBrushEdit);
    }
    case WM_CLOSE: {
        if (ctx) ctx->confirmed = false;
        DestroyWindow(hwnd);
        return 0;
    }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static std::string ShowPromptDialog(HWND hParent, const char* title, const char* label, const std::string& defaultName) {
    static bool registered = false;
    HINSTANCE hInst = GetModuleHandle(NULL);
    if (!registered) {
        WNDCLASSA wc = { 0 };
        wc.lpfnWndProc = PromptWndProc;
        wc.hInstance = hInst;
        wc.hbrBackground = g_hBrushCard;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = "osuPoint_PromptDialog";
        RegisterClassA(&wc);
        registered = true;
    }

    PromptContext ctx{};
    strncpy_s(ctx.title, sizeof(ctx.title), title, _TRUNCATE);
    strncpy_s(ctx.label, sizeof(ctx.label), label, _TRUNCATE);
    strncpy_s(ctx.name, sizeof(ctx.name), defaultName.c_str(), _TRUNCATE);
    ctx.confirmed = false;
    ctx.hFont = g_hFont;

    RECT parentRc;
    GetWindowRect(hParent, &parentRc);
    int pw = 270;
    int ph = 145;
    int px = parentRc.left + ((parentRc.right - parentRc.left) - pw) / 2;
    int py = parentRc.top + ((parentRc.bottom - parentRc.top) - ph) / 2;

    HWND hPrompt = CreateWindowExA(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        "osuPoint_PromptDialog", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        px, py, pw, ph, hParent, NULL, hInst, &ctx);

    if (!hPrompt) return "";

    BOOL darkMode = !g_isLightTheme;
    DwmSetWindowAttribute(hPrompt, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));

    EnableWindow(hParent, FALSE);
    MSG msg;
    while (IsWindow(hPrompt) && GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            SendMessage(hPrompt, WM_COMMAND, IDCANCEL, 0);
            continue;
        }
        if (!IsDialogMessage(hPrompt, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);

    if (ctx.confirmed) {
        return Trim(ctx.name);
    }
    return "";
}

static std::string PromptPresetName(HWND hParent, const std::string& defaultName) {
    return ShowPromptDialog(hParent, "Save Area Preset", "Preset Name:", defaultName);
}

static std::string PromptPaletteName(HWND hParent, const std::string& defaultName) {
    return ShowPromptDialog(hParent, "Save Custom Theme", "Palette Name:", defaultName);
}

static HICON CreateAppIcon() {
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
                        r_sum += GetRValue(g_colorAccent);
                        g_sum += GetGValue(g_colorAccent);
                        b_sum += GetBValue(g_colorAccent);
                        a_sum += 255;
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

static int HitTestCanvas(int mx, int my) {
    if (g_lockDrag) return -1;

    auto distSq = [](POINT p, int x, int y) {
        int dx = p.x - x, dy = p.y - y;
        return dx * dx + dy * dy;
    };

    int hitRadius = (std::max)(8, static_cast<int>(std::round(8.0 * g_uiScale)));
    int hitLimit = hitRadius * hitRadius;

    for (int i = 0; i < 4; ++i) {
        if (distSq(g_previewCorners[i], mx, my) <= hitLimit) {
            return i;
        }
    }

    if (distSq(g_previewCenter, mx, my) <= hitLimit) {
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

static void DrawPreview(HDC hdc) {
    int scaled_preview_x = static_cast<int>(PREVIEW_X * g_uiScale);
    int scaled_preview_y = static_cast<int>(PREVIEW_Y * g_uiScale);
    int scaled_preview_max_w = static_cast<int>(PREVIEW_MAX_W * g_uiScale);
    int scaled_preview_max_h = static_cast<int>(PREVIEW_MAX_H * g_uiScale);
    int scaled_bezel = static_cast<int>(BEZEL * g_uiScale);

    int active_max_w = scaled_preview_max_w - 2 * scaled_bezel;
    int active_max_h = scaled_preview_max_h - 2 * scaled_bezel;

    double pw = (g_spec.phys_w > 0.0) ? g_spec.phys_w : 152.0;
    double ph = (g_spec.phys_h > 0.0) ? g_spec.phys_h : 95.0;

    double scaleX = static_cast<double>(active_max_w) / pw;
    double scaleY = static_cast<double>(active_max_h) / ph;
    double s = (std::min)(scaleX, scaleY);
    g_previewScale = s;

    int active_w = static_cast<int>(std::round(pw * s));
    int active_h = static_cast<int>(std::round(ph * s));

    int offset_x = (active_max_w - active_w) / 2;
    int offset_y = (active_max_h - active_h) / 2;

    int body_x = scaled_preview_x + offset_x;
    int body_y = scaled_preview_y + offset_y;
    int body_w = active_w + 2 * scaled_bezel;
    int body_h = active_h + 2 * scaled_bezel;

    SelectObject(hdc, g_hBrushPreviewBezel);
    SelectObject(hdc, g_hPenCardBorder);
    int bRad = static_cast<int>(std::round(8 * g_uiScale));
    RoundRect(hdc, body_x, body_y, body_x + body_w, body_y + body_h, bRad, bRad);

    int act_left = body_x + scaled_bezel;
    int act_top = body_y + scaled_bezel;
    g_originX = act_left;
    g_originY = act_top;

    SelectObject(hdc, g_hBrushPreviewActive);
    SelectObject(hdc, g_hPenEditBorder);
    Rectangle(hdc, act_left, act_top, act_left + active_w, act_top + active_h);

    TransformConfig cfg = g_cfgStore.Load();
    double rad = cfg.rotation_deg * (PI / 180.0);
    double cos_val = std::cos(rad);
    double sin_val = std::sin(rad);

    double hw = cfg.width_mm * 0.5;
    double hh = cfg.height_mm * 0.5;

    double corners_x[4] = { -hw,  hw,  hw, -hw };
    double corners_y[4] = { -hh, -hh,  hh,  hh };

    POINT polyPts[4];
    Gdiplus::PointF gfPts[4];
    for (int i = 0; i < 4; ++i) {
        double rx = corners_x[i] * cos_val - corners_y[i] * sin_val;
        double ry = corners_x[i] * sin_val + corners_y[i] * cos_val;
        double fx = act_left + (cfg.cx + rx) * s;
        double fy = act_top + (cfg.cy + ry) * s;
        gfPts[i].X = static_cast<float>(fx);
        gfPts[i].Y = static_cast<float>(fy);
        int sx = static_cast<int>(std::round(fx));
        int sy = static_cast<int>(std::round(fy));
        polyPts[i] = { sx, sy };
        g_previewCorners[i] = { sx, sy };
    }
    g_previewCenter = {
        act_left + static_cast<int>(std::round(cfg.cx * s)),
        act_top + static_cast<int>(std::round(cfg.cy * s))
    };

    BYTE ar = GetRValue(g_colorAccent);
    BYTE ag = GetGValue(g_colorAccent);
    BYTE ab = GetBValue(g_colorAccent);

    if (g_antialiasing) {
        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

        Gdiplus::SolidBrush fillBrush(Gdiplus::Color(55, ar, ag, ab));
        g.FillPolygon(&fillBrush, gfPts, 4);

        Gdiplus::Pen outlinePen(Gdiplus::Color(255, ar, ag, ab), 1.6f);
        outlinePen.SetLineJoin(Gdiplus::LineJoinRound);
        g.DrawPolygon(&outlinePen, gfPts, 4);

        float cx = static_cast<float>(act_left + cfg.cx * s);
        float cy = static_cast<float>(act_top + cfg.cy * s);
        g.DrawLine(&outlinePen, cx - 6.0f, cy, cx + 6.0f, cy);
        g.DrawLine(&outlinePen, cx, cy - 6.0f, cx, cy + 6.0f);

        if (!g_lockDrag) {
            float hr = static_cast<float>((std::max)(3, static_cast<int>(std::round(3.5 * g_uiScale))));
            COLORREF dotBg = g_isLightTheme ? RGB(40, 40, 48) : RGB(240, 240, 245);
            Gdiplus::SolidBrush dotBrush(Gdiplus::Color(255, GetRValue(dotBg), GetGValue(dotBg), GetBValue(dotBg)));
            Gdiplus::Pen dotPen(Gdiplus::Color(255, ar, ag, ab), 1.0f);
            for (int i = 0; i < 4; ++i) {
                float px = gfPts[i].X;
                float py = gfPts[i].Y;
                g.FillEllipse(&dotBrush, px - hr, py - hr, hr * 2.0f, hr * 2.0f);
                g.DrawEllipse(&dotPen, px - hr, py - hr, hr * 2.0f, hr * 2.0f);
            }
        }
    }
    else {
        HGDIOBJ oldBrush = SelectObject(hdc, g_hBrushAccentSubtle);
        HGDIOBJ oldPen = SelectObject(hdc, g_hPenAccent);
        Polygon(hdc, polyPts, 4);

        if (!g_lockDrag) {
            SelectObject(hdc, g_hBrushText);
            SelectObject(hdc, g_hPenAccentThin);
            int hr = (std::max)(3, static_cast<int>(std::round(3.5 * g_uiScale)));
            for (int i = 0; i < 4; ++i) {
                Ellipse(hdc, g_previewCorners[i].x - hr, g_previewCorners[i].y - hr,
                    g_previewCorners[i].x + hr + 1, g_previewCorners[i].y + hr + 1);
            }
        }

        SelectObject(hdc, g_hPenAccent);
        MoveToEx(hdc, g_previewCenter.x - 5, g_previewCenter.y, NULL);
        LineTo(hdc, g_previewCenter.x + 6, g_previewCenter.y);
        MoveToEx(hdc, g_previewCenter.x, g_previewCenter.y - 5, NULL);
        LineTo(hdc, g_previewCenter.x, g_previewCenter.y + 6);

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
    }
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
    SetTimer(hwnd, 1, 500, NULL);
    UpdateStatusText();
    CheckConfigFileReload();
    if (g_debugMode) SetupDebugConsole(true);
}

void ShutdownDriver() {
    static std::atomic<bool> s_shuttingDown{ false };
    if (s_shuttingDown.exchange(true)) return;

    if (g_hwnd) KillTimer(g_hwnd, 1);

    StopDriverPipeline();

    if (g_hDevNotify) {
        UnregisterDeviceNotification(g_hDevNotify);
        g_hDevNotify = NULL;
    }

    DisableSubMillisecondTimer();
    SaveConfig();

    if (g_nid.hWnd) {
        Shell_NotifyIconA(NIM_DELETE, &g_nid);
        g_nid.hWnd = NULL;
    }

    DeleteGdiObjects(
        g_hBrushBg, g_hBrushCard, g_hBrushEdit, g_hPenCardBorder, g_hPenEditBorder,
        g_hBrushAccent, g_hBrushAccentSubtle, g_hPenAccent, g_hBrushText,
        g_hPenAccentThin, g_hBrushPreviewBezel, g_hBrushPreviewActive,
        g_hFont, g_hFontSmall
    );
    if (s_hCustomIcon) { DestroyIcon(s_hCustomIcon); s_hCustomIcon = NULL; }

    if (s_pOriginalBgBitmap) { delete s_pOriginalBgBitmap; s_pOriginalBgBitmap = nullptr; }
    if (s_pBgBitmap) { delete s_pBgBitmap; s_pBgBitmap = nullptr; }
    FreeBackingBuffer();
    if (g_gdiplusToken) { Gdiplus::GdiplusShutdown(g_gdiplusToken); g_gdiplusToken = 0; }

    PostQuitMessage(0);
}

static void BlitBackdrop(HDC destDC, HWND controlHwnd, int w, int h) {
    if (s_pBgBitmap) {
        if (!s_hBackingDC || g_backingDirty) RenderBackingBuffer(g_hwnd);
        if (s_hBackingDC) {
            POINT pt = { 0, 0 };
            MapWindowPoints(controlHwnd, g_hwnd, &pt, 1);
            BitBlt(destDC, 0, 0, w, h, s_hBackingDC, pt.x, pt.y, SRCCOPY);
            return;
        }
    }
    RECT r = { 0, 0, w, h };
    FillRect(destDC, &r, g_hBrushCard);
}

static WNDPROC g_origEditProc = NULL;

static LRESULT CALLBACK EditSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCALCSIZE) {
        if (wParam) {
            auto* p = reinterpret_cast<LPNCCALCSIZE_PARAMS>(lParam);
            p->rgrc[0].left += 2;
            p->rgrc[0].top += 2;
            p->rgrc[0].right -= 2;
            p->rgrc[0].bottom -= 2;
        } else {
            auto* prc = reinterpret_cast<LPRECT>(lParam);
            prc->left += 2;
            prc->top += 2;
            prc->right -= 2;
            prc->bottom -= 2;
        }
        return 0;
    }
    if (msg == WM_NCPAINT) {
        HDC hdc = GetWindowDC(hWnd);
        if (hdc) {
            RECT wr;
            GetWindowRect(hWnd, &wr);
            int w = wr.right - wr.left;
            int h = wr.bottom - wr.top;
            if (w > 0 && h > 0) {
                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP memBm = CreateCompatibleBitmap(hdc, w, h);
                HGDIOBJ oldBm = SelectObject(memDC, memBm);

                BlitBackdrop(memDC, hWnd, w, h);

                bool hasFocus = (GetFocus() == hWnd);
                HBRUSH hEditBg = CreateSolidBrush(COLOR_EDIT_BG);
                HPEN hPen = CreatePen(PS_SOLID, 1, hasFocus ? g_colorAccent : COLOR_BORDER);
                HGDIOBJ oldBrush = SelectObject(memDC, hEditBg);
                HGDIOBJ oldPen = SelectObject(memDC, hPen);

                RoundRect(memDC, 0, 0, w, h, 6, 6);

                SelectObject(memDC, oldBrush);
                SelectObject(memDC, oldPen);
                DeleteObject(hEditBg);
                DeleteObject(hPen);

                RECT cr;
                GetClientRect(hWnd, &cr);
                POINT ptTopLeft = { cr.left, cr.top };
                POINT ptBottomRight = { cr.right, cr.bottom };
                ClientToScreen(hWnd, &ptTopLeft);
                ClientToScreen(hWnd, &ptBottomRight);
                RECT screenCr = {
                    ptTopLeft.x - wr.left,
                    ptTopLeft.y - wr.top,
                    ptBottomRight.x - wr.left,
                    ptBottomRight.y - wr.top
                };

                if (screenCr.right > screenCr.left && screenCr.bottom > screenCr.top) {
                    ExcludeClipRect(hdc, screenCr.left, screenCr.top, screenCr.right, screenCr.bottom);
                }
                BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

                SelectObject(memDC, oldBm);
                DeleteObject(memBm);
                DeleteDC(memDC);
            }
            ReleaseDC(hWnd, hdc);
        }
        return 0;
    }
    if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS) {
        SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    if (hWnd == hRatio && msg == WM_CHAR) {
        char ch = static_cast<char>(wParam);
        if (ch >= '0' && ch <= '9') {
            char buf[64] = "";
            GetWindowTextA(hWnd, buf, sizeof(buf));
            DWORD selStart = 0, selEnd = 0;
            SendMessage(hWnd, EM_GETSEL, reinterpret_cast<WPARAM>(&selStart), reinterpret_cast<LPARAM>(&selEnd));
            std::string s = buf;
            if (selStart <= s.size() && selEnd <= s.size()) {
                s.erase(selStart, selEnd - selStart);
                s.insert(selStart, 1, ch);
            }
            size_t dotPos = s.find_first_of(".,");
            if (dotPos != std::string::npos && s.find(':') == std::string::npos) {
                size_t decDigits = s.length() - (dotPos + 1);
                if (static_cast<int>(decDigits) > g_ratioPrecision) {
                    MessageBeep(MB_OK);
                    return 0;
                }
            }
        }
    }
    if (!g_origEditProc) return DefWindowProcA(hWnd, msg, wParam, lParam);
    return CallWindowProcA(g_origEditProc, hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK CboSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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

        BlitBackdrop(memDC, hWnd, w, h);

        bool hasFocus = (GetFocus() == hWnd);
        HBRUSH bgBrush = CreateSolidBrush(COLOR_EDIT_BG);
        HPEN borderPen = CreatePen(PS_SOLID, 1, hasFocus ? g_colorAccent : COLOR_BORDER);
        HGDIOBJ oldBrush = SelectObject(memDC, bgBrush);
        HGDIOBJ oldPen = SelectObject(memDC, borderPen);
        RoundRect(memDC, rc.left, rc.top, rc.right, rc.bottom, 6, 6);
        SelectObject(memDC, oldBrush);
        SelectObject(memDC, oldPen);
        DeleteObject(bgBrush);
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
        textRc.left += static_cast<int>(std::round(8 * g_uiScale));
        textRc.right -= static_cast<int>(std::round(20 * g_uiScale));
        DrawTextA(memDC, textBuf, -1, &textRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(memDC, oldFont);

        int arrowX = rc.right - static_cast<int>(std::round(11 * g_uiScale));
        int arrowY = (rc.top + rc.bottom) / 2;
        int aw = (std::max)(3, static_cast<int>(std::round(4 * g_uiScale)));
        int ah = (std::max)(2, static_cast<int>(std::round(3 * g_uiScale)));
        POINT arrowPts[3] = {
            { arrowX - aw, arrowY - ah + 1 },
            { arrowX + aw, arrowY - ah + 1 },
            { arrowX,      arrowY + ah }
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

void UpdateAppFonts(int dpi) {
    if (dpi <= 0) dpi = 96;
    if (g_hFont) { DeleteObject(g_hFont); g_hFont = NULL; }
    if (g_hFontSmall) { DeleteObject(g_hFontSmall); g_hFontSmall = NULL; }

    int hMain = -MulDiv(10, dpi, 72);
    int hSmall = -MulDiv(9, dpi, 72);

    g_hFont = CreateFontA(hMain, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, "Segoe UI Variable Text");
    if (!g_hFont) {
        g_hFont = CreateFontA(hMain, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    }

    g_hFontSmall = CreateFontA(hSmall, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, "Segoe UI Variable Text");
    if (!g_hFontSmall) {
        g_hFontSmall = CreateFontA(hSmall, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    }

    HWND uiControls[] = {
        btnTabMain, btnTabSettings,
        lblMon, hCboMon,
        lblPreset, hCboPreset, btnSavePreset, btnDelPreset,
        lblW, hW, lblH, hH, lblCX, hCX, lblCY, hCY, lblRot, hRot,
        chkRatio, hRatio, btnAutoRatio, btnArea,
        chkLockDrag, chkClipArea, chkPenClick, chkAntiAlias, chkDebugMode,
        chkExtrapolate, lblExtraTau, btnExtraDec, btnExtraVal, btnExtraInc,
        btnSetBg, btnClearBg, chkCopyBgImage, chkDeleteOldBg, lblOpacity, btnOpacityDec, btnOpacityVal, btnOpacityInc,
        lblBlur, btnBlurDec, btnBlurVal, btnBlurInc,
        lblPalette, hCboPalette, btnPickPalette, btnSavePalette, btnDelPalette,
        lblRatioPrec, btnRatioPrecDec, btnRatioPrecVal, btnRatioPrecInc,
        btnReset
    };
    for (HWND c : uiControls) {
        if (c) {
            bool useSmall = (c == btnAutoRatio || c == btnArea || c == btnSavePreset || c == btnDelPreset ||
                             c == btnSetBg || c == btnClearBg || c == btnOpacityDec || c == btnOpacityVal ||
                             c == btnOpacityInc || c == btnBlurDec || c == btnBlurVal || c == btnBlurInc ||
                             c == btnPickPalette || c == btnSavePalette || c == btnDelPalette ||
                             c == btnExtraDec || c == btnExtraVal || c == btnExtraInc ||
                             c == btnRatioPrecDec || c == btnRatioPrecVal || c == btnRatioPrecInc);
            SendMessage(c, WM_SETFONT, reinterpret_cast<WPARAM>(useSmall ? g_hFontSmall : g_hFont), TRUE);
        }
    }
}

void SwitchTab(int tabIndex) {
    g_activeTab = (tabIndex == 1) ? 1 : 0;

    HWND mainControls[] = {
        lblMon, hCboMon,
        lblPreset, hCboPreset, btnSavePreset, btnDelPreset,
        lblW, hW, lblH, hH, lblCX, hCX, lblCY, hCY, lblRot, hRot,
        chkRatio, hRatio, btnAutoRatio, btnArea,
        chkLockDrag, chkClipArea, chkPenClick
    };

    HWND settingsControls[] = {
        lblPalette, hCboPalette, btnPickPalette, btnSavePalette, btnDelPalette,
        btnSetBg, btnClearBg, chkCopyBgImage, chkDeleteOldBg, lblOpacity, btnOpacityDec, btnOpacityVal, btnOpacityInc,
        lblBlur, btnBlurDec, btnBlurVal, btnBlurInc,
        chkAntiAlias, chkDebugMode,
        chkExtrapolate, lblExtraTau, btnExtraDec, btnExtraVal, btnExtraInc,
        lblRatioPrec, btnRatioPrecDec, btnRatioPrecVal, btnRatioPrecInc,
        btnReset
    };

    if (g_activeTab == 0) {
        for (HWND c : settingsControls) { if (c) ShowWindow(c, SW_HIDE); }
        for (HWND c : mainControls) { if (c) ShowWindow(c, SW_SHOW); }
    } else {
        for (HWND c : mainControls) { if (c) ShowWindow(c, SW_HIDE); }
        for (HWND c : settingsControls) { if (c) ShowWindow(c, SW_SHOW); }
    }

    if (btnTabMain) InvalidateRect(btnTabMain, NULL, FALSE);
    if (btnTabSettings) InvalidateRect(btnTabSettings, NULL, FALSE);

    const double sc = g_uiScale;
    auto S = [sc](int v) noexcept -> int { return static_cast<int>(std::round(v * sc)); };
    RECT leftCardRc = { S(LEFT_CARD_X), S(LEFT_CARD_Y), S(LEFT_CARD_X + LEFT_CARD_W), S(LEFT_CARD_Y + LEFT_CARD_H) };
    RedrawWindow(g_hwnd, &leftCardRc, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_UPDATENOW);

    SaveConfig();
}

void RelayoutUI(HWND hwnd) {
    if (!hwnd) return;
    const double sc = g_uiScale;
    auto S = [sc](int v) noexcept -> int {
        return static_cast<int>(std::round(v * sc));
    };

    if (hCboMon) {
        SendMessage(hCboMon, CB_SETDROPPEDWIDTH, 0, 0);
    }
    if (hCboPreset) {
        SendMessage(hCboPreset, CB_SETDROPPEDWIDTH, 0, 0);
    }
    if (hCboPalette) {
        SendMessage(hCboPalette, CB_SETDROPPEDWIDTH, 0, 0);
    }

    struct LayoutItem {
        HWND hwnd;
        int x, y, w, h;
    };

    LayoutItem items[] = {
        // Tab switcher
        { btnTabMain,      18,  18, 138,  24 },
        { btnTabSettings, 160,  18, 138,  24 },

        // --- TAB 0: AREA CONTROLS ---
        // Display
        { lblMon,         18,  49,  55,  20 },
        { hCboMon,        76,  47, 222, 140 },

        // Presets
        { lblPreset,      18,  75,  55,  20 },
        { hCboPreset,     76,  73, 130, 140 },
        { btnSavePreset, 210,  73,  42,  24 },
        { btnDelPreset,  256,  73,  42,  24 },

        // Numeric area fields
        { lblW,           18, 103,  92,  20 },
        { hW,            112, 101, 186,  24 },

        { lblH,           18, 129,  92,  20 },
        { hH,            112, 127, 186,  24 },

        { lblCX,          18, 155,  92,  20 },
        { hCX,           112, 153, 186,  24 },

        { lblCY,          18, 181,  92,  20 },
        { hCY,           112, 179, 186,  24 },

        { lblRot,         18, 207,  92,  20 },
        { hRot,          112, 205, 186,  24 },

        // Ratio group
        { chkRatio,       18, 233,  54,  24 },
        { hRatio,         76, 233,  68,  24 },
        { btnAutoRatio,  148, 233,  72,  24 },
        { btnArea,       224, 233,  74,  24 },

        // Checkboxes: Lock Area & Clip Area
        { chkLockDrag,    18, 261, 136,  22 },
        { chkClipArea,   160, 261, 138,  22 },

        // Checkbox: Pen Click
        { chkPenClick,    18, 287, 280,  22 },

        // --- TAB 1: SETTINGS CONTROLS ---
        // Theme / Palette selector
        { lblPalette,     18,  49,  55,  20 },
        { hCboPalette,    76,  47, 140, 140 },
        { btnPickPalette,220,  47,  78,  24 },

        { btnSavePalette, 76,  74, 108,  24 },
        { btnDelPalette, 190,  74, 108,  24 },

        // Background Image
        { btnSetBg,       18, 104, 138,  24 },
        { btnClearBg,    160, 104, 138,  24 },

        // Background Image Copy & Delete Options
        { chkCopyBgImage, 18, 130, 136,  20 },
        { chkDeleteOldBg,160, 130, 138,  20 },

        // Opacity
        { lblOpacity,     18, 154,  64,  20 },
        { btnOpacityDec,  86, 152,  40,  22 },
        { btnOpacityVal, 130, 152, 120,  22 },
        { btnOpacityInc, 254, 152,  44,  22 },

        // Blur
        { lblBlur,        18, 180,  64,  20 },
        { btnBlurDec,     86, 178,  40,  22 },
        { btnBlurVal,    130, 178, 120,  22 },
        { btnBlurInc,    254, 178,  44,  22 },

        // Checkboxes: Smooth AA & Debug Mode
        { chkAntiAlias,   18, 204, 136,  22 },
        { chkDebugMode,  160, 204, 138,  22 },

        // Kinematic Extrapolation: Toggle and Tau ms adjustment
        { chkExtrapolate, 18, 230, 108,  22 },
        { lblExtraTau,   130, 232,  34,  18 },
        { btnExtraDec,   166, 230,  32,  22 },
        { btnExtraVal,   202, 230,  60,  22 },
        { btnExtraInc,   266, 230,  32,  22 },

        // Ratio Precision
        { lblRatioPrec,   18, 256, 140,  18 },
        { btnRatioPrecDec,166, 254,  32,  22 },
        { btnRatioPrecVal,202, 254,  60,  22 },
        { btnRatioPrecInc,266, 254,  32,  22 },

        // Full Area Reset Button
        { btnReset,       18, 280, 280,  24 }
    };

    HDWP hdwp = BeginDeferWindowPos(static_cast<int>(sizeof(items) / sizeof(items[0])));
    for (const auto& it : items) {
        if (it.hwnd) {
            if (hdwp) {
                hdwp = DeferWindowPos(hdwp, it.hwnd, NULL,
                    S(it.x), S(it.y), S(it.w), S(it.h),
                    SWP_NOZORDER | SWP_NOACTIVATE);
            } else {
                SetWindowPos(it.hwnd, NULL,
                    S(it.x), S(it.y), S(it.w), S(it.h),
                    SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
    }
    if (hdwp) {
        EndDeferWindowPos(hdwp);
    }

    SwitchTab(g_activeTab);
}

void UpdateStatusText() noexcept {
    if (!g_hwnd || !IsWindowVisible(g_hwnd)) return;

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
    else 
    if (g_hDevice.load(std::memory_order_acquire) != INVALID_HANDLE_VALUE) {
        static uint64_t s_lastReports = 0;
        static double s_hzEma = 0.0;
        uint64_t curReports = g_reportsReceived.load(std::memory_order_relaxed);
        if (curReports < s_lastReports) {
            s_lastReports = 0;
            s_hzEma = 0.0;
        }
        double hz = (curReports - s_lastReports) / 0.5;
        s_lastReports = curReports;
        s_hzEma = (s_hzEma == 0.0) ? hz : (s_hzEma * 0.3 + hz * 0.7);

        int bestEp = 0;
        uint64_t maxEp = 0;
        for (int i=0; i<4; ++i) {
            uint64_t val = g_epCount[i].load(std::memory_order_relaxed);
            if (val > maxEp) { maxEp = val; bestEp = i; }
        }

        int eps = g_numActiveEndpoints.load(std::memory_order_relaxed);
        DWORD lastErr = g_lastReadError.load(std::memory_order_relaxed);
        uint64_t w_ok = g_wacomStats.accepted.load(std::memory_order_relaxed);
        uint64_t w_rej = g_wacomStats.rejected.load(std::memory_order_relaxed);
        uint64_t dedup = g_reportsDeduped.load(std::memory_order_relaxed);
        
        double dedupPct = (w_ok > 0) ? (dedup * 100.0 / w_ok) : 0.0;
        
        uint32_t lat_p50 = 0, lat_p99 = 0, lat_max = 0, lat_min = 0;
        g_latencyStats.Compute(lat_p50, lat_p99, lat_max, lat_min);

        uint32_t jit_stddev = 0, jit_avgintv = 0;
        g_jitterStats.Compute(jit_stddev, jit_avgintv);
        uint32_t jit = g_jitterStats.GetJitterUs();
        if (jit == 0 && jit_stddev > 0) jit = jit_stddev;

        if (g_debugMode) {
            snprintf(title, sizeof(title), "%s | %.0f Hz (EP%d) | lat:%uus/%uus | jit:\xC2\xB1%uus | ok:%.1fk rej:%llu | err:%lu",
                     g_spec.name.c_str(), s_hzEma, bestEp, lat_p50, lat_p99, jit, w_ok / 1000.0, w_rej, lastErr);
            snprintf(tip, sizeof(tip), "%s | %.0f Hz (EP%d) | lat:%uus | jit:\xC2\xB1%uus | ok:%.1fk",
                     g_spec.name.c_str(), s_hzEma, bestEp, lat_p50, jit, w_ok / 1000.0);
        } else {
            snprintf(title, sizeof(title), "%s", g_spec.name.c_str());
            snprintf(tip, sizeof(tip), "%s", g_spec.name.c_str());
        }
                 
        if (g_debugMode) {
            char conTitle[256];
            snprintf(conTitle, sizeof(conTitle), "osu!Point Debug - %s | %.0f Hz (EP%d) | Lat: %uus | Jit: \xC2\xB1%uus | Ok: %.1fk",
                     g_spec.name.c_str(), s_hzEma, bestEp, lat_p50, jit, w_ok / 1000.0);
            SetConsoleTitleA(conTitle);

            ConsoleTelemetry ct;
            ct.connected = true;
            ct.deviceName = g_spec.name;
            ct.vid = g_spec.vid;
            ct.pid = g_spec.pid;
            ct.phys_w = g_spec.phys_w;
            ct.phys_h = g_spec.phys_h;
            ct.max_raw_x = static_cast<uint32_t>(g_spec.max_x);
            ct.max_raw_y = static_cast<uint32_t>(g_spec.max_y);
            ct.max_pressure = (g_spec.vid == VendorID::Wacom && g_spec.pid == 0x037A) ? 2048 : (g_spec.vid == VendorID::Wacom ? 4096 : 8192);

            if (g_target_monitor_idx >= 0 && g_target_monitor_idx < static_cast<int>(g_monitors.size())) {
                const auto& m = g_monitors[g_target_monitor_idx];
                ct.monIdx = g_target_monitor_idx;
                ct.monW = m.rc.right - m.rc.left;
                ct.monH = m.rc.bottom - m.rc.top;
                ct.monX = m.rc.left;
                ct.monY = m.rc.top;
            }
            ct.deskW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            ct.deskH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            if (ct.deskW == 0) ct.deskW = GetSystemMetrics(SM_CXSCREEN);
            if (ct.deskH == 0) ct.deskH = GetSystemMetrics(SM_CYSCREEN);

            ct.areaW = ParseInput(hW);
            ct.areaH = ParseInput(hH);
            ct.areaCX = ParseInput(hCX);
            ct.areaCY = ParseInput(hCY);
            ct.areaRot = ParseInput(hRot);
            ct.areaRatio = g_configRatio;
            ct.clipArea = g_clipArea;
            ct.lockDrag = g_lockDrag;

            CompactPenSample cur = g_livePen.current;
            ct.penRawX = cur.raw_x;
            ct.penRawY = cur.raw_y;
            ct.penPx = cur.px;
            ct.penPy = cur.py;
            ct.penUn = cur.un;
            ct.penVn = cur.vn;
            ct.penPressure = cur.pressure;
            ct.penTip = (cur.tip != 0);
            ct.penBarrel = (cur.barrel != 0);
            ct.penInRange = (cur.in_range != 0);

            uint32_t widx = g_livePen.write_idx.load(std::memory_order_relaxed);
            size_t count = (std::min)(static_cast<size_t>(widx), LivePenTelemetry::TRAJ_CAP);
            ct.historyCount = count;
            for (size_t i = 0; i < count; ++i) {
                size_t slot = (widx - count + i) % LivePenTelemetry::TRAJ_CAP;
                const auto& hs = g_livePen.history[slot];
                ct.history[i].rx = hs.raw_x;
                ct.history[i].ry = hs.raw_y;
                ct.history[i].px = hs.px;
                ct.history[i].py = hs.py;
                ct.history[i].p = hs.pressure;
                ct.history[i].tip = (hs.tip != 0);
            }

            ct.hz = s_hzEma;
            ct.ep = bestEp;
            ct.okCount = w_ok;
            ct.rejCount = w_rej;
            ct.dedupPct = dedupPct;
            ct.extrapEnabled = g_extrapolation;
            ct.extrapTau = g_extrapolationTau;
            ct.lat_p50 = lat_p50;
            ct.lat_p99 = lat_p99;
            ct.lat_max = lat_max;
            ct.jit = jit;
            ct.lastErr = lastErr;
            ct.connCount = eps;
            UpdatePinnedTelemetry(ct);
        }
    }

    else {
        snprintf(title, sizeof(title), "osu!Point - [Waiting for tablet...]");
        snprintf(tip, sizeof(tip), "osu!Point - Waiting for tablet...");
        if (g_debugMode) {
            SetConsoleTitleA("osu!Point Debug - Waiting for tablet...");
            ConsoleTelemetry ct;
            ct.connected = false;
            if (g_target_monitor_idx >= 0 && g_target_monitor_idx < static_cast<int>(g_monitors.size())) {
                const auto& m = g_monitors[g_target_monitor_idx];
                ct.monIdx = g_target_monitor_idx;
                ct.monW = m.rc.right - m.rc.left;
                ct.monH = m.rc.bottom - m.rc.top;
                ct.monX = m.rc.left;
                ct.monY = m.rc.top;
            }
            ct.deskW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            ct.deskH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            if (ct.deskW == 0) ct.deskW = GetSystemMetrics(SM_CXSCREEN);
            if (ct.deskH == 0) ct.deskH = GetSystemMetrics(SM_CYSCREEN);
            ct.areaW = ParseInput(hW);
            ct.areaH = ParseInput(hH);
            ct.areaCX = ParseInput(hCX);
            ct.areaCY = ParseInput(hCY);
            ct.areaRot = ParseInput(hRot);
            ct.areaRatio = g_configRatio;
            ct.clipArea = g_clipArea;
            ct.lockDrag = g_lockDrag;
            UpdatePinnedTelemetry(ct);
        }
    }

    SetWindowTextIfChanged(g_hwnd, title);
    strncpy_s(g_nid.szTip, sizeof(g_nid.szTip), tip, _TRUNCATE);
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);
}

static std::string GetComboSelectedText(HWND hCbo) {
    if (!hCbo) return "";
    int sel = static_cast<int>(SendMessage(hCbo, CB_GETCURSEL, 0, 0));
    if (sel == CB_ERR) return "";
    char buf[64] = "";
    SendMessageA(hCbo, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(buf));
    return std::string(buf);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_INPUT) {
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    
    if (msg == 0x02E0) { // WM_DPICHANGED
        int newDpi = HIWORD(wParam);
        if (newDpi <= 0) newDpi = 96;
        g_uiScale = newDpi / 96.0;

        RECT* prcNewWindow = reinterpret_cast<RECT*>(lParam);
        RECT scaledRc = { 0, 0, static_cast<int>(std::round(BASE_WIDTH * g_uiScale)), static_cast<int>(std::round(BASE_HEIGHT * g_uiScale)) };
        DWORD winStyle = static_cast<DWORD>(GetWindowLongPtrA(hwnd, GWL_STYLE));
        AdjustWindowRectForDpi(&scaledRc, winStyle, newDpi);

        SetWindowPos(hwnd, NULL, prcNewWindow->left, prcNewWindow->top,
            scaledRc.right - scaledRc.left,
            scaledRc.bottom - scaledRc.top,
            SWP_NOZORDER | SWP_NOACTIVATE);

        g_backingDirty = true;
        UpdateAppFonts(newDpi);
        RelayoutUI(hwnd);
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
        return 0;
    }
    if (msg == WM_DEVICECHANGE) {
        if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
            auto* di = reinterpret_cast<PDEV_BROADCAST_DEVICEINTERFACE_A>(lParam);
            if (di && di->dbcc_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                if (wParam == DBT_DEVICEREMOVECOMPLETE) {
                    extern char g_removedPath[260];
                    extern std::atomic<bool> g_removalPending;
                    strncpy_s(g_removedPath, sizeof(g_removedPath), di->dbcc_name, _TRUNCATE);
                    g_removalPending.store(true, std::memory_order_release);
                }
            }
            if (g_hDeviceChangeEvent) {
                SetEvent(g_hDeviceChangeEvent);
            }
        }
        return TRUE;
    }
    static const UINT s_wmTabletQuery = RegisterWindowMessageA("TabletQuerySystemGestureStatus");
    if (s_wmTabletQuery && msg == s_wmTabletQuery) {
        return 0x00000001 | 0x00000008 | 0x00000010 | 0x00000100 | 0x00000200 | 0x00008000 | 0x00010000 | 0x00080000 | 0x00100000;
    }
    if (msg == WM_DISPLAYCHANGE) {
        RefreshMonitors();
        UpdateValues();
        UpdateStatusText();
        return 0;
    }
    if (msg == WM_TIMER && wParam == 1) {
        CheckConfigFileReload();
        UpdateStatusText();
        return 0;
    }
    if (msg == WM_USER + 10) {
        UpdateStatusText();
        return 0;
    }
    if (msg == WM_USER + 11) {
        if (g_nid.hWnd) {
            g_nid.uFlags |= NIF_INFO;
            g_nid.dwInfoFlags = NIIF_WARNING;
            strncpy_s(g_nid.szInfoTitle, sizeof(g_nid.szInfoTitle), "osu!Point Conflict", _TRUNCATE);
            strncpy_s(g_nid.szInfo, sizeof(g_nid.szInfo), "CONFLICT: Close Wacom Driver / Use HidHide. Cursor will jerk!", _TRUNCATE);
            Shell_NotifyIconA(NIM_MODIFY, &g_nid);
            g_nid.uFlags &= ~NIF_INFO;
        }
        return 0;
    }
    if (msg == WM_ACTIVATE && LOWORD(wParam) != WA_INACTIVE) {
        CheckConfigFileReload();
    }
    if (msg == WM_SYSCOMMAND && (wParam & 0xFFF0) == SC_MINIMIZE) {
        ShowWindow(hwnd, SW_HIDE);
        KillTimer(hwnd, 1);
        HWND hCon = GetConsoleWindow();
        if (hCon) ShowWindow(hCon, SW_HIDE);
        return 0;
    }
    if (msg == WM_SIZE && wParam == SIZE_MINIMIZED) {
        ShowWindow(hwnd, SW_HIDE);
        KillTimer(hwnd, 1);
        HWND hCon = GetConsoleWindow();
        if (hCon) ShowWindow(hCon, SW_HIDE);
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

        static int s_lastMx = -1, s_lastMy = -1;
        if (mx == s_lastMx && my == s_lastMy) return 0;
        s_lastMx = mx;
        s_lastMy = my;

        if (g_dragMode == DragMode::Move) {
            double dx = (mx - g_dragStartMouse.x) / g_previewScale;
            double dy = (my - g_dragStartMouse.y) / g_previewScale;
            double ncx = g_dragOrigCX + dx;
            double ncy = g_dragOrigCY + dy;

            ncx = (std::max)(0.0, (std::min)(g_spec.phys_w, ncx));
            ncy = (std::max)(0.0, (std::min)(g_spec.phys_h, ncy));

            g_isUpdatingUI = true;
            SetWindowTextIfChanged(hCX, FormatDouble(ncx));
            SetWindowTextIfChanged(hCY, FormatDouble(ncy));
            g_isUpdatingUI = false;

            TransformConfig cfg = g_cfgStore.Load();
            SetTransform(cfg.width_mm, cfg.height_mm, ncx, ncy, cfg.rotation_deg, g_target_monitor_idx);
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

            nw = (std::max)(2.0, (std::min)(g_spec.phys_w, nw));
            nh = (std::max)(2.0, (std::min)(g_spec.phys_h, nh));

            g_isUpdatingUI = true;
            SetWindowTextIfChanged(hW, FormatDouble(nw));
            SetWindowTextIfChanged(hH, FormatDouble(nh));
            g_isUpdatingUI = false;

            SetTransform(nw, nh, cfg.cx, cfg.cy, cfg.rotation_deg, g_target_monitor_idx);
        }

        const double sc = g_uiScale;
        auto S = [sc](int v) noexcept -> int { return static_cast<int>(std::round(v * sc)); };
        RECT rightCardRc = { S(RIGHT_CARD_X), S(RIGHT_CARD_Y), S(RIGHT_CARD_X + RIGHT_CARD_W), S(RIGHT_CARD_Y + RIGHT_CARD_H) };
        InvalidateRect(hwnd, &rightCardRc, FALSE);
        return 0;
    }
    if (msg == WM_LBUTTONUP && g_dragMode != DragMode::None) {
        ReleaseCapture();
        g_dragMode = DragMode::None;
        SaveConfig();
        return 0;
    }

    if (msg == WM_COMMAND) {
        if (LOWORD(wParam) == 1050) {
            SwitchTab(0);
            return 0;
        }
        if (LOWORD(wParam) == 1051) {
            SwitchTab(1);
            return 0;
        }
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
        if (LOWORD(wParam) == 1007) {
            g_debugMode = !g_debugMode;
            if (chkDebugMode) InvalidateRect(chkDebugMode, NULL, FALSE);
            SetupDebugConsole(g_debugMode);
            if (g_debugMode) {
                g_wacomStats.accepted.store(0, std::memory_order_relaxed);
                g_wacomStats.rejected.store(0, std::memory_order_relaxed);
                g_wacomStats.glitches.store(0, std::memory_order_relaxed);
                g_reportsReceived.store(0, std::memory_order_relaxed);
                g_reportsDeduped.store(0, std::memory_order_relaxed);
                for (int i = 0; i < 4; ++i) g_epCount[i].store(0, std::memory_order_relaxed);
            }
            UpdateStatusText();
            SaveConfig();
        }
        if (LOWORD(wParam) == 1003) {
            AutoMatchRatio();
        }
        if (LOWORD(wParam) == 1008) {
            AutoMatchAreaRatio();
        }
        if (LOWORD(wParam) == 1009) {
            g_antialiasing = !g_antialiasing;
            if (chkAntiAlias) InvalidateRect(chkAntiAlias, NULL, FALSE);
            InvalidateRect(hwnd, NULL, FALSE);
            SaveConfig();
        }
        if (LOWORD(wParam) == 1025) {
            g_extrapolation = !g_extrapolation;
            if (chkExtrapolate) InvalidateRect(chkExtrapolate, NULL, FALSE);
            SaveConfig();
        }
        if (LOWORD(wParam) == 1010) {
            std::string pBuf = GetComboSelectedText(hCboPreset);
            std::string curName = !pBuf.empty() ? pBuf : g_activePresetName;
            std::string newName = PromptPresetName(hwnd, curName);
            if (!newName.empty()) {
                SavePreset(newName);
            }
        }
        if (LOWORD(wParam) == 1011) {
            std::string pBuf = GetComboSelectedText(hCboPreset);
            if (!pBuf.empty()) {
                DeletePreset(pBuf);
            }
        }
        if (LOWORD(wParam) == 1012 && HIWORD(wParam) == CBN_SELCHANGE) {
            int sel = static_cast<int>(SendMessage(hCboPreset, CB_GETCURSEL, 0, 0));
            if (sel != CB_ERR && sel < static_cast<int>(g_presets.size())) {
                ApplyPreset(g_presets[sel].name);
            }
        }
        if (LOWORD(wParam) == 1013 && HIWORD(wParam) == CBN_SELCHANGE) {
            int sel = static_cast<int>(SendMessage(hCboPalette, CB_GETCURSEL, 0, 0));
            if (sel >= 0 && sel < static_cast<int>(g_numPalettes)) {
                g_activePalette = g_availablePalettes[sel].id;
                UpdateSystemAccentPalette();
                g_bgImagePath = "";
                LoadBackgroundImage("");
                TriggerBackingRedraw(hwnd);
                SaveConfig();
            } else if (sel >= static_cast<int>(g_numPalettes) && sel < static_cast<int>(g_numPalettes + g_customPalettes.size())) {
                size_t custIdx = sel - g_numPalettes;
                g_activePalette = g_customPalettes[custIdx].name;
                UpdateSystemAccentPalette();
                g_bgImagePath = g_customPalettes[custIdx].bg_image;
                LoadBackgroundImage(g_bgImagePath);
                TriggerBackingRedraw(hwnd);
                SaveConfig();
            }
        }
        if (LOWORD(wParam) == 1016) {
            static COLORREF s_custColors[16] = { 0 };
            CHOOSECOLORA cc{};
            cc.lStructSize = sizeof(cc);
            cc.hwndOwner = hwnd;
            cc.rgbResult = g_colorAccent;
            cc.lpCustColors = s_custColors;
            cc.Flags = CC_FULLOPEN | CC_RGBINIT;
            if (ChooseColorA(&cc)) {
                g_activePalette = "Custom";
                SaveCustomPalette("Custom", cc.rgbResult, g_bgImagePath);
                UpdateSystemAccentPalette();
                RefreshPaletteCombo();
                SaveConfig();
                g_backingDirty = true;
                RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
            }
        }
        if (LOWORD(wParam) == 1017) {
            std::string pBuf = GetComboSelectedText(hCboPalette);
            std::string curName = !pBuf.empty() ? pBuf : g_activePalette;
            if (_stricmp(curName.c_str(), "system") == 0 || _stricmp(curName.c_str(), "System Accent") == 0) curName = "Custom";
            std::string newName = PromptPaletteName(hwnd, curName);
            if (!newName.empty()) {
                SaveCustomPalette(newName, g_colorAccent, g_bgImagePath);
                g_activePalette = newName;
                UpdateSystemAccentPalette();
                RefreshPaletteCombo();
                SaveConfig();
                g_backingDirty = true;
                RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
            }
        }
        if (LOWORD(wParam) == 1018) {
            std::string pBuf = GetComboSelectedText(hCboPalette);
            if (!pBuf.empty()) {
                DeleteCustomPalette(pBuf);
                RefreshPaletteCombo();
                g_backingDirty = true;
                RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
            }
        }
        if (LOWORD(wParam) == 1019) g_uiOpacity = (std::max)(10, g_uiOpacity - 5);
        if (LOWORD(wParam) == 1020) g_uiOpacity = 80;
        if (LOWORD(wParam) == 1021) g_uiOpacity = (std::min)(100, g_uiOpacity + 5);
        if (LOWORD(wParam) >= 1019 && LOWORD(wParam) <= 1021) {
            UpdateOpacityDisplay();
            TriggerBackingRedraw(hwnd);
        }
        if (LOWORD(wParam) == 1022) g_bgBlur = (std::max)(0, g_bgBlur - 2);
        if (LOWORD(wParam) == 1023) g_bgBlur = (g_bgBlur == 0) ? 6 : 0;
        if (LOWORD(wParam) == 1024) g_bgBlur = (std::min)(20, g_bgBlur + 2);
        if (LOWORD(wParam) >= 1022 && LOWORD(wParam) <= 1024) {
            UpdateBlurDisplay();
            ApplyBgBlur();
            TriggerBackingRedraw(hwnd);
        }
        if (LOWORD(wParam) == 1026) g_extrapolationTau = (std::max)(1.0, g_extrapolationTau - 0.5);
        if (LOWORD(wParam) == 1027) g_extrapolationTau = 2.5;
        if (LOWORD(wParam) == 1028) g_extrapolationTau = (std::min)(5.0, g_extrapolationTau + 0.5);
        if (LOWORD(wParam) >= 1026 && LOWORD(wParam) <= 1028) {
            UpdateExtrapolateDisplay();
            SaveConfig();
        }
        if (LOWORD(wParam) == 1029) g_ratioPrecision = (std::max)(1, g_ratioPrecision - 1);
        if (LOWORD(wParam) == 1030) g_ratioPrecision = 2;
        if (LOWORD(wParam) == 1031) g_ratioPrecision = (std::min)(8, g_ratioPrecision + 1);
        if (LOWORD(wParam) >= 1029 && LOWORD(wParam) <= 1031) {
            UpdateRatioPrecisionDisplay();
            SaveConfig();
        }
        if (LOWORD(wParam) == 1014) {
            char szFile[MAX_PATH] = "";
            OPENFILENAMEA ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = "Image Files (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = szFile;
            ofn.nMaxFile = sizeof(szFile);
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameA(&ofn)) {
                std::string oldImg = g_bgImagePath;
                std::string newPath;

                if (g_copyBgImage) {
                    const char* fname = strrchr(szFile, '\\');
                    if (!fname) fname = strrchr(szFile, '/');
                    fname = fname ? (fname + 1) : szFile;
                    std::string targetPath = g_baseDir + fname;
                    if (_stricmp(szFile, targetPath.c_str()) != 0) {
                        DWORD attr = GetFileAttributesA(targetPath.c_str());
                        if (attr == INVALID_FILE_ATTRIBUTES) {
                            CopyFileA(szFile, targetPath.c_str(), FALSE);
                        }
                    }
                    auto it = std::find_if(g_copiedBgImages.begin(), g_copiedBgImages.end(),
                        [&](const std::string& item) { return _stricmp(item.c_str(), fname) == 0; });
                    if (it == g_copiedBgImages.end()) {
                        g_copiedBgImages.push_back(fname);
                    }
                    newPath = fname;
                } else {
                    newPath = szFile;
                }

                if (g_deleteOldBgImage && !oldImg.empty() && _stricmp(oldImg.c_str(), newPath.c_str()) != 0) {
                    const char* oldFname = strrchr(oldImg.c_str(), '\\');
                    if (!oldFname) oldFname = strrchr(oldImg.c_str(), '/');
                    oldFname = oldFname ? (oldFname + 1) : oldImg.c_str();

                    auto trackIt = std::find_if(g_copiedBgImages.begin(), g_copiedBgImages.end(),
                        [&](const std::string& item) {
                            return _stricmp(item.c_str(), oldImg.c_str()) == 0 || _stricmp(item.c_str(), oldFname) == 0;
                        });

                    if (trackIt != g_copiedBgImages.end()) {
                        std::string oldFullPath = oldImg;
                        if (oldFullPath.find(':') == std::string::npos && oldFullPath.rfind("\\\\", 0) != 0) {
                            oldFullPath = g_baseDir + oldImg;
                        }
                        bool isInsideBase = (_strnicmp(oldFullPath.c_str(), g_baseDir.c_str(), g_baseDir.length()) == 0);
                        bool hasNoTraversal = (oldFullPath.find("..") == std::string::npos);
                        const char* ext = strrchr(oldFullPath.c_str(), '.');
                        bool validExt = ext && (_stricmp(ext, ".png") == 0 || _stricmp(ext, ".jpg") == 0 ||
                                                _stricmp(ext, ".jpeg") == 0 || _stricmp(ext, ".bmp") == 0);

                        std::string curNewFull = newPath;
                        if (curNewFull.find(':') == std::string::npos && curNewFull.rfind("\\\\", 0) != 0) {
                            curNewFull = g_baseDir + newPath;
                        }

                        if (isInsideBase && hasNoTraversal && validExt && _stricmp(oldFullPath.c_str(), curNewFull.c_str()) != 0) {
                            if (s_pOriginalBgBitmap) {
                                delete s_pOriginalBgBitmap;
                                s_pOriginalBgBitmap = nullptr;
                            }
                            if (s_pBgBitmap) {
                                delete s_pBgBitmap;
                                s_pBgBitmap = nullptr;
                            }
                            DeleteFileA(oldFullPath.c_str());
                            g_copiedBgImages.erase(trackIt);
                        }
                    }
                }

                g_bgImagePath = newPath;
                LoadBackgroundImage(g_bgImagePath);
                TriggerBackingRedraw(hwnd);
                for (auto& cp : g_customPalettes) {
                    if (_stricmp(cp.name.c_str(), g_activePalette.c_str()) == 0) {
                        cp.bg_image = g_bgImagePath;
                        break;
                    }
                }
                SaveConfig();
            }
        }
        if (LOWORD(wParam) == 1032) {
            g_copyBgImage = !g_copyBgImage;
            if (chkCopyBgImage) InvalidateRect(chkCopyBgImage, NULL, FALSE);
            SaveConfig();
        }
        if (LOWORD(wParam) == 1033) {
            g_deleteOldBgImage = !g_deleteOldBgImage;
            if (chkDeleteOldBg) InvalidateRect(chkDeleteOldBg, NULL, FALSE);
            SaveConfig();
        }
        if (LOWORD(wParam) == 1015) {
            g_bgImagePath = "";
            LoadBackgroundImage("");
            TriggerBackingRedraw(hwnd);
            for (auto& cp : g_customPalettes) {
                if (_stricmp(cp.name.c_str(), g_activePalette.c_str()) == 0) {
                    cp.bg_image = "";
                    break;
                }
            }
            SaveConfig();
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
            if (src == hRatio) {
                char rBuf[64] = "";
                GetWindowTextA(hRatio, rBuf, sizeof(rBuf));
                std::string s = rBuf;
                size_t dotPos = s.find_first_of(".,");
                if (dotPos != std::string::npos && s.find(':') == std::string::npos) {
                    size_t decDigits = s.length() - (dotPos + 1);
                    if (static_cast<int>(decDigits) > g_ratioPrecision) {
                        s = s.substr(0, dotPos + 1 + g_ratioPrecision);
                        g_isUpdatingUI = true;
                        SetWindowTextA(hRatio, s.c_str());
                        SendMessage(hRatio, EM_SETSEL, s.length(), s.length());
                        g_isUpdatingUI = false;
                    }
                }
            }
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
        HWND hStatic = reinterpret_cast<HWND>(lParam);
        SetBkMode(hdc, TRANSPARENT);
        if (s_pBgBitmap) {
            if (s_hBackingDC && hStatic) {
                RECT rc;
                GetClientRect(hStatic, &rc);
                POINT pt = { 0, 0 };
                MapWindowPoints(hStatic, hwnd, &pt, 1);
                int rw = rc.right - rc.left;
                int rh = rc.bottom - rc.top;
                BitBlt(hdc, 0, 0, rw, rh, s_hBackingDC, pt.x, pt.y, SRCCOPY);

                double lum = GetBackingLuminance(pt.x, pt.y, rw, rh);
                bool isDarkBg = (lum <= 135.0);
                COLORREF mainTextColor = isDarkBg ? RGB(245, 245, 250) : RGB(15, 15, 20);
                COLORREF shadowColor = isDarkBg ? RGB(0, 0, 0) : RGB(255, 255, 255);

                char text[128] = "";
                GetWindowTextA(hStatic, text, sizeof(text));
                if (text[0] != '\0') {
                    RECT shadowRc = rc;
                    OffsetRect(&shadowRc, 1, 1);
                    SetTextColor(hdc, shadowColor);
                    SelectObject(hdc, g_hFont);
                    DrawTextA(hdc, text, -1, &shadowRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                }

                SetTextColor(hdc, mainTextColor);
            } else {
                SetTextColor(hdc, COLOR_TEXT);
            }
            return reinterpret_cast<LRESULT>(GetStockObject(HOLLOW_BRUSH));
        }
        SetTextColor(hdc, COLOR_TEXT);
        SetBkColor(hdc, COLOR_CARD_BG);
        return reinterpret_cast<LRESULT>(g_hBrushCard);
    }
    if (msg == WM_DRAWITEM) {
        auto pDIS = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);

        BlitBackdrop(pDIS->hDC, pDIS->hwndItem, pDIS->rcItem.right, pDIS->rcItem.bottom);

        // Tab switcher buttons
        if (pDIS->CtlID == 1050 || pDIS->CtlID == 1051) {
            bool isSelected = (pDIS->CtlID == 1050) ? (g_activeTab == 0) : (g_activeTab == 1);
            COLORREF bgCol = isSelected ? (g_isLightTheme ? RGB(225, 225, 235) : RGB(36, 36, 44)) : (g_isLightTheme ? RGB(242, 242, 248) : RGB(26, 26, 32));
            COLORREF borderCol = isSelected ? g_colorAccent : g_theme.cardBorder;
            HBRUSH tabBg = CreateSolidBrush(bgCol);
            HPEN tabPen = CreatePen(PS_SOLID, 1, borderCol);
            HGDIOBJ ob = SelectObject(pDIS->hDC, tabBg);
            HGDIOBJ op = SelectObject(pDIS->hDC, tabPen);
            RoundRect(pDIS->hDC, pDIS->rcItem.left, pDIS->rcItem.top, pDIS->rcItem.right, pDIS->rcItem.bottom, 6, 6);
            if (isSelected) {
                HPEN accPen = CreatePen(PS_SOLID, 2, g_colorAccent);
                HGDIOBJ oldAcc = SelectObject(pDIS->hDC, accPen);
                MoveToEx(pDIS->hDC, pDIS->rcItem.left + 8, pDIS->rcItem.bottom - 2, NULL);
                LineTo(pDIS->hDC, pDIS->rcItem.right - 8, pDIS->rcItem.bottom - 2);
                SelectObject(pDIS->hDC, oldAcc);
                DeleteObject(accPen);
            }
            SetBkMode(pDIS->hDC, TRANSPARENT);
            COLORREF tabTextCol = isSelected ? COLOR_TEXT : (g_isLightTheme ? RGB(70, 70, 85) : RGB(190, 190, 205));
            SetTextColor(pDIS->hDC, tabTextCol);
            SelectObject(pDIS->hDC, g_hFont);
            const char* tabLabel = (pDIS->CtlID == 1050) ? "Area" : "Settings";
            DrawTextA(pDIS->hDC, tabLabel, -1, &pDIS->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(pDIS->hDC, ob);
            SelectObject(pDIS->hDC, op);
            DeleteGdiObjects(tabBg, tabPen);
            return TRUE;
        }

        // Owner-drawn buttons
        if (pDIS->CtlID == 1001 || pDIS->CtlID == 1003 || pDIS->CtlID == 1008 ||
            pDIS->CtlID == 1010 || pDIS->CtlID == 1011 || pDIS->CtlID == 1014 || pDIS->CtlID == 1015 ||
            pDIS->CtlID == 1016 || pDIS->CtlID == 1017 || pDIS->CtlID == 1018 ||
            pDIS->CtlID == 1019 || pDIS->CtlID == 1020 || pDIS->CtlID == 1021 ||
            pDIS->CtlID == 1022 || pDIS->CtlID == 1023 || pDIS->CtlID == 1024 ||
            pDIS->CtlID == 1026 || pDIS->CtlID == 1027 || pDIS->CtlID == 1028 ||
            pDIS->CtlID == 1029 || pDIS->CtlID == 1030 || pDIS->CtlID == 1031) {

            bool pressed = (pDIS->itemState & ODS_SELECTED);
            HBRUSH btnBrush = CreateSolidBrush(pressed ? g_colorAccentPressed : COLOR_EDIT_BG);
            HPEN btnPen = CreatePen(PS_SOLID, 1, pressed ? g_colorAccent : COLOR_BORDER);
            HGDIOBJ ob = SelectObject(pDIS->hDC, btnBrush);
            HGDIOBJ op = SelectObject(pDIS->hDC, btnPen);

            RoundRect(pDIS->hDC, pDIS->rcItem.left, pDIS->rcItem.top, pDIS->rcItem.right, pDIS->rcItem.bottom, 6, 6);

            SetBkMode(pDIS->hDC, TRANSPARENT);
            SetTextColor(pDIS->hDC, pressed ? RGB(255, 255, 255) : COLOR_TEXT);
            SelectObject(pDIS->hDC, (pDIS->CtlID != 1001) ? g_hFontSmall : g_hFont);

            char btnText[64] = "";
            GetWindowTextA(pDIS->hwndItem, btnText, sizeof(btnText));
            const char* label = btnText;
            switch (pDIS->CtlID) {
                case 1001: label = "Full Area (Reset)"; break;
                case 1003: label = "Screen"; break;
                case 1008: label = "Area"; break;
                case 1010: case 1017: label = "Save"; break;
                case 1011: case 1018: label = "Del"; break;
                case 1014: label = "+ Image"; break;
                case 1015: label = "X"; break;
                case 1016: label = "Pick"; break;
                case 1019: case 1022: case 1026: case 1029: label = "-"; break;
                case 1021: case 1024: case 1028: case 1031: label = "+"; break;
                default: break;
            }
            DrawTextA(pDIS->hDC, label, -1, &pDIS->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(pDIS->hDC, ob);
            SelectObject(pDIS->hDC, op);
            DeleteGdiObjects(btnBrush, btnPen);
            return TRUE;
        }

        // Owner-drawn checkboxes
        if (pDIS->CtlID == 1002 || pDIS->CtlID == 1004 || pDIS->CtlID == 1005 || pDIS->CtlID == 1006 || pDIS->CtlID == 1007 || pDIS->CtlID == 1009 || pDIS->CtlID == 1025 || pDIS->CtlID == 1032 || pDIS->CtlID == 1033) {
            bool isChecked = false;
            const char* label = "";
            switch (pDIS->CtlID) {
                case 1002: isChecked = g_lockRatio; label = "Ratio:"; break;
                case 1004: isChecked = g_lockDrag; label = "Lock Area"; break;
                case 1005: isChecked = g_clipArea; label = "Clip Area"; break;
                case 1006: isChecked = g_penClick.load(std::memory_order_relaxed); label = "Pen Click"; break;
                case 1007: isChecked = g_debugMode; label = "Debug Mode"; break;
                case 1009: isChecked = g_antialiasing; label = "Smooth AA"; break;
                case 1025: isChecked = g_extrapolation; label = "Extrapolate"; break;
                case 1032: isChecked = g_copyBgImage; label = "Copy to App"; break;
                case 1033: isChecked = g_deleteOldBgImage; label = "Delete Old"; break;
                default: break;
            }

            int boxSize = (std::max)(14, static_cast<int>(std::round(14.0 * g_uiScale)));
            int boxY = (pDIS->rcItem.bottom - pDIS->rcItem.top - boxSize) / 2;
            RECT boxRc = { 0, boxY, boxSize, boxY + boxSize };

            HBRUSH boxBg = CreateSolidBrush(isChecked ? g_colorAccent : COLOR_EDIT_BG);
            HPEN boxPen = CreatePen(PS_SOLID, 1, isChecked ? g_colorAccent : COLOR_BORDER);
            HGDIOBJ ob = SelectObject(pDIS->hDC, boxBg);
            HGDIOBJ op = SelectObject(pDIS->hDC, boxPen);

            RoundRect(pDIS->hDC, boxRc.left, boxRc.top, boxRc.right, boxRc.bottom, 4, 4);

            if (isChecked) {
                HPEN whitePen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
                HGDIOBJ oldWhitePen = SelectObject(pDIS->hDC, whitePen);
                int bx = boxRc.left;
                int by = boxRc.top;
                int bs = boxSize;
                POINT chkPts[3] = {
                    { bx + static_cast<int>(bs * 0.22), by + static_cast<int>(bs * 0.50) },
                    { bx + static_cast<int>(bs * 0.44), by + static_cast<int>(bs * 0.72) },
                    { bx + static_cast<int>(bs * 0.78), by + static_cast<int>(bs * 0.28) }
                };
                Polyline(pDIS->hDC, chkPts, 3);
                SelectObject(pDIS->hDC, oldWhitePen);
                DeleteObject(whitePen);
            }

            SelectObject(pDIS->hDC, ob);
            SelectObject(pDIS->hDC, op);
            DeleteGdiObjects(boxBg, boxPen);

            SetBkMode(pDIS->hDC, TRANSPARENT);
            RECT txtRc = pDIS->rcItem;
            txtRc.left += boxSize + (std::max)(4, static_cast<int>(std::round(5.0 * g_uiScale)));

            COLORREF chkTextCol = COLOR_TEXT;
            if (s_pBgBitmap) {
                POINT pt = { 0, 0 };
                MapWindowPoints(pDIS->hwndItem, hwnd, &pt, 1);
                int tw = txtRc.right - txtRc.left;
                int th = txtRc.bottom - txtRc.top;
                double lum = GetBackingLuminance(pt.x + txtRc.left, pt.y, tw, th);
                bool isDarkBg = (lum <= 135.0);
                chkTextCol = isDarkBg ? RGB(245, 245, 250) : RGB(15, 15, 20);
                COLORREF shadowCol = isDarkBg ? RGB(0, 0, 0) : RGB(255, 255, 255);

                RECT shadowRc = txtRc;
                OffsetRect(&shadowRc, 1, 1);
                SetTextColor(pDIS->hDC, shadowCol);
                SelectObject(pDIS->hDC, g_hFont);
                DrawTextA(pDIS->hDC, label, -1, &shadowRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            }

            SetTextColor(pDIS->hDC, chkTextCol);
            SelectObject(pDIS->hDC, g_hFont);
            DrawTextA(pDIS->hDC, label, -1, &txtRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            return TRUE;
        }
    }
    if (msg == WM_ERASEBKGND) {
        return 1;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        if (g_backingDirty || !s_hBackingDC) {
            RenderBackingBuffer(hwnd);
        }

        if (s_hBackingDC) {
            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;

            if (!s_hMemDC) {
                s_hMemDC = CreateCompatibleDC(hdc);
            }
            if (!s_hMemBm || s_memW != w || s_memH != h) {
                if (s_hMemBm) {
                    SelectObject(s_hMemDC, s_hMemOldBm);
                    DeleteObject(s_hMemBm);
                }
                s_hMemBm = CreateCompatibleBitmap(hdc, w, h);
                s_memW = w;
                s_memH = h;
                s_hMemOldBm = reinterpret_cast<HBITMAP>(SelectObject(s_hMemDC, s_hMemBm));
            }

            BitBlt(s_hMemDC, 0, 0, w, h, s_hBackingDC, 0, 0, SRCCOPY);
            DrawPreview(s_hMemDC);

            int pw = ps.rcPaint.right - ps.rcPaint.left;
            int ph = ps.rcPaint.bottom - ps.rcPaint.top;
            if (pw > 0 && ph > 0) {
                BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top, pw, ph,
                       s_hMemDC, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
            } else {
                BitBlt(hdc, 0, 0, w, h, s_hMemDC, 0, 0, SRCCOPY);
            }
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_DWMCOLORIZATIONCOLORCHANGED || msg == WM_SETTINGCHANGE) {
        UpdateSystemAccentPalette();
        RefreshPaletteCombo();
        g_backingDirty = true;
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
        return 0;
    }
    if (msg == WM_POWERBROADCAST) {
        if (wParam == PBT_APMRESUMEAUTOMATIC) {
            LogMessage("WM_POWERBROADCAST: PBT_APMRESUMEAUTOMATIC - Forcing re-handshake");
            g_rawReportsReceived.store(0, std::memory_order_relaxed);
        }
        return TRUE;
    }
    if (msg == WM_CLOSE || msg == WM_DESTROY) {
        ShutdownDriver();
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool InitUI(HINSTANCE hInst, int nCmdShow) {
    Gdiplus::GdiplusStartupInput gdiStartup;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiStartup, NULL);

    UpdateSystemAccentPalette();

    UpdateAppFonts(96);

    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hbrBackground = g_hBrushBg;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconA(hInst, MAKEINTRESOURCEA(1));
    wc.lpszClassName = "osuPoint_Driver";
    RegisterClassA(&wc);

    DWORD winStyle = (WS_OVERLAPPEDWINDOW ^ (WS_THICKFRAME | WS_MAXIMIZEBOX));
    RECT winRc = { 0, 0, BASE_WIDTH, BASE_HEIGHT };
    AdjustWindowRect(&winRc, winStyle, FALSE);
    int winWidth = winRc.right - winRc.left;
    int winHeight = winRc.bottom - winRc.top;

    g_hwnd = CreateWindowA("osuPoint_Driver", "osu!Point",
        winStyle, CW_USEDEFAULT, CW_USEDEFAULT, winWidth, winHeight,
        NULL, NULL, hInst, NULL);
    if (!g_hwnd) return false;

    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    DEV_BROADCAST_DEVICEINTERFACE_A devFilter{};
    devFilter.dbcc_size = sizeof(devFilter);
    devFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    devFilter.dbcc_classguid = hidGuid;
    g_hDevNotify = RegisterDeviceNotificationA(g_hwnd, &devFilter, DEVICE_NOTIFY_WINDOW_HANDLE);

    BOOL useDarkMode = !g_isLightTheme;
    if (FAILED(DwmSetWindowAttribute(g_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDarkMode, sizeof(useDarkMode)))) {
        DwmSetWindowAttribute(g_hwnd, 19, &useDarkMode, sizeof(useDarkMode));
    }
    int backdrop = 2; // DWMSBT_MAINWINDOW (Mica)
    DwmSetWindowAttribute(g_hwnd, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &backdrop, sizeof(backdrop));
    int cornerPref = 2; // DWMWCP_ROUND
    DwmSetWindowAttribute(g_hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &cornerPref, sizeof(cornerPref));

    HICON hIcon = wc.hIcon;
    if (!hIcon) {
        hIcon = CreateAppIcon();
        s_hCustomIcon = hIcon;
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
        if (g_needsConflictWarning.exchange(false, std::memory_order_relaxed)) {
            PostMessageA(g_hwnd, WM_USER + 11, 0, 0);
        }
    }

    TransformConfig initialCfg = g_cfgStore.Load();

    int dpi = GetDpiForWindow(g_hwnd);
    if (dpi <= 0) dpi = 96;
    g_uiScale = dpi / 96.0;

    UpdateAppFonts(dpi);

    RECT scaledRc = { 0, 0, static_cast<int>(std::round(BASE_WIDTH * g_uiScale)), static_cast<int>(std::round(BASE_HEIGHT * g_uiScale)) };
    AdjustWindowRectForDpi(&scaledRc, winStyle, dpi);
    SetWindowPos(g_hwnd, NULL, 0, 0, scaledRc.right - scaledRc.left, scaledRc.bottom - scaledRc.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    if (!g_bgImagePath.empty()) {
        LoadBackgroundImage(g_bgImagePath);
    }

    btnTabMain = CreateWindowA("BUTTON", "Area", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1050), NULL, NULL);
    btnTabSettings = CreateWindowA("BUTTON", "Settings", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1051), NULL, NULL);

    lblMon = CreateWindowA("STATIC", "Display:", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, g_hwnd, NULL, NULL, NULL);
    hCboMon = CreateWindowA("COMBOBOX", "", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 0, 0, g_hwnd, NULL, NULL, NULL);
    g_origCboProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(hCboMon, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(CboSubclassProc)));

    lblPreset = CreateWindowA("STATIC", "Preset:", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, g_hwnd, NULL, NULL, NULL);
    hCboPreset = CreateWindowA("COMBOBOX", "", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1012), NULL, NULL);
    btnSavePreset = CreateWindowA("BUTTON", "Save", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1010), NULL, NULL);
    btnDelPreset = CreateWindowA("BUTTON", "Del", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1011), NULL, NULL);
    SetWindowLongPtrA(hCboPreset, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(CboSubclassProc));

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

    lblW = CreateWindowA("STATIC", "Width (mm):", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, g_hwnd, NULL, NULL, NULL);
    hW = CreateWindowA("EDIT", FormatDouble(initialCfg.width_mm).c_str(), WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, g_hwnd, NULL, NULL, NULL);

    lblH = CreateWindowA("STATIC", "Height (mm):", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, g_hwnd, NULL, NULL, NULL);
    hH = CreateWindowA("EDIT", FormatDouble(initialCfg.height_mm).c_str(), WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, g_hwnd, NULL, NULL, NULL);

    lblCX = CreateWindowA("STATIC", "Center X:", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, g_hwnd, NULL, NULL, NULL);
    hCX = CreateWindowA("EDIT", FormatDouble(initialCfg.cx).c_str(), WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, g_hwnd, NULL, NULL, NULL);

    lblCY = CreateWindowA("STATIC", "Center Y:", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, g_hwnd, NULL, NULL, NULL);
    hCY = CreateWindowA("EDIT", FormatDouble(initialCfg.cy).c_str(), WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, g_hwnd, NULL, NULL, NULL);

    lblRot = CreateWindowA("STATIC", "Rotation:", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, g_hwnd, NULL, NULL, NULL);
    hRot = CreateWindowA("EDIT", FormatDouble(initialCfg.rotation_deg).c_str(), WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, g_hwnd, NULL, NULL, NULL);

    chkRatio = CreateWindowA("BUTTON", "Ratio:", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1002), NULL, NULL);
    hRatio = CreateWindowA("EDIT", g_configRatio.c_str(), WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, g_hwnd, NULL, NULL, NULL);
    btnAutoRatio = CreateWindowA("BUTTON", "Screen", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1003), NULL, NULL);
    btnArea = CreateWindowA("BUTTON", "Area", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1008), NULL, NULL);

    chkLockDrag = CreateWindowA("BUTTON", "Lock Area", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1004), NULL, NULL);
    chkClipArea = CreateWindowA("BUTTON", "Clip Area", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1005), NULL, NULL);
    chkPenClick = CreateWindowA("BUTTON", "Pen Click", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1006), NULL, NULL);
    chkAntiAlias = CreateWindowA("BUTTON", "Smooth AA", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1009), NULL, NULL);

    chkDebugMode = CreateWindowA("BUTTON", "Debug Mode", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1007), NULL, NULL);
    chkExtrapolate = CreateWindowA("BUTTON", "Extrapolate", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1025), NULL, NULL);
    lblExtraTau = CreateWindowA("STATIC", "Tau:", WS_CHILD, 0, 0, 0, 0, g_hwnd, NULL, NULL, NULL);
    btnExtraDec = CreateWindowA("BUTTON", "-", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1026), NULL, NULL);
    char extraBuf[32];
    snprintf(extraBuf, sizeof(extraBuf), "%.1f ms", g_extrapolationTau);
    btnExtraVal = CreateWindowA("BUTTON", extraBuf, WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1027), NULL, NULL);
    btnExtraInc = CreateWindowA("BUTTON", "+", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1028), NULL, NULL);
    btnSetBg = CreateWindowA("BUTTON", "+ Image", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1014), NULL, NULL);
    btnClearBg = CreateWindowA("BUTTON", "X", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1015), NULL, NULL);
    chkCopyBgImage = CreateWindowA("BUTTON", "Copy to App", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1032), NULL, NULL);
    chkDeleteOldBg = CreateWindowA("BUTTON", "Delete Old", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1033), NULL, NULL);
    lblOpacity = CreateWindowA("STATIC", "Opacity:", WS_CHILD, 0, 0, 0, 0, g_hwnd, NULL, NULL, NULL);
    btnOpacityDec = CreateWindowA("BUTTON", "-", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1019), NULL, NULL);
    char opBuf[16];
    snprintf(opBuf, sizeof(opBuf), "%d%%", g_uiOpacity);
    btnOpacityVal = CreateWindowA("BUTTON", opBuf, WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1020), NULL, NULL);
    btnOpacityInc = CreateWindowA("BUTTON", "+", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1021), NULL, NULL);

    lblBlur = CreateWindowA("STATIC", "Blur:", WS_CHILD, 0, 0, 0, 0, g_hwnd, NULL, NULL, NULL);
    btnBlurDec = CreateWindowA("BUTTON", "-", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1022), NULL, NULL);
    char blurBuf[16];
    if (g_bgBlur <= 0) snprintf(blurBuf, sizeof(blurBuf), "Off");
    else snprintf(blurBuf, sizeof(blurBuf), "%d px", g_bgBlur);
    btnBlurVal = CreateWindowA("BUTTON", blurBuf, WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1023), NULL, NULL);
    btnBlurInc = CreateWindowA("BUTTON", "+", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1024), NULL, NULL);

    lblPalette = CreateWindowA("STATIC", "Theme:", WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, g_hwnd, NULL, NULL, NULL);
    hCboPalette = CreateWindowA("COMBOBOX", "", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1013), NULL, NULL);
    SetWindowLongPtrA(hCboPalette, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(CboSubclassProc));
    btnPickPalette = CreateWindowA("BUTTON", "Pick", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1016), NULL, NULL);
    btnSavePalette = CreateWindowA("BUTTON", "Save", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1017), NULL, NULL);
    btnDelPalette = CreateWindowA("BUTTON", "Del", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1018), NULL, NULL);

    lblRatioPrec = CreateWindowA("STATIC", "Ratio Decimals:", WS_CHILD, 0, 0, 0, 0, g_hwnd, NULL, NULL, NULL);
    btnRatioPrecDec = CreateWindowA("BUTTON", "-", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1029), NULL, NULL);
    char precBuf[16];
    snprintf(precBuf, sizeof(precBuf), "%d digits", g_ratioPrecision);
    btnRatioPrecVal = CreateWindowA("BUTTON", precBuf, WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1030), NULL, NULL);
    btnRatioPrecInc = CreateWindowA("BUTTON", "+", WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1031), NULL, NULL);

    btnReset = CreateWindowA("BUTTON", "Full Area (Reset)", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 0, 0, 0, 0, g_hwnd, reinterpret_cast<HMENU>(1001), NULL, NULL);

    RefreshPresetCombo();
    RefreshPaletteCombo();
    UpdateOpacityDisplay();
    UpdateBlurDisplay();
    UpdateExtrapolateDisplay();
    UpdateRatioPrecisionDisplay();

    HWND editBoxes[] = { hW, hH, hCX, hCY, hRot, hRatio };
    for (HWND ed : editBoxes) {
        if (ed) {
            if (!g_origEditProc) {
                g_origEditProc = reinterpret_cast<WNDPROC>(GetWindowLongPtrA(ed, GWLP_WNDPROC));
            }
            SetWindowLongPtrA(ed, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditSubclassProc));
            SendMessage(ed, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 6));
        }
    }

    UpdateAppFonts(dpi);
    RelayoutUI(g_hwnd);
    SwitchTab(g_activeTab);

    g_guiReady = true;
    UpdateValues();
    UpdateStatusText();

    SetTimer(g_hwnd, 1, 500, NULL);
    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    return true;
}

int RunUIMessageLoop() {
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return static_cast<int>(msg.wParam);
}
