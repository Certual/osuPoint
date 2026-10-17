#pragma once
#include "common.h"

extern HWND g_hwnd;
extern bool g_guiReady;
extern bool g_isUpdatingUI;

extern HWND btnTabMain, btnTabSettings;
extern HWND lblMon, hCboMon;
extern HWND lblPreset, hCboPreset, btnSavePreset, btnDelPreset;
extern HWND lblW, hW, lblH, hH, lblCX, hCX, lblCY, hCY, lblRot, hRot;
extern HWND chkRatio, hRatio, btnAutoRatio, btnArea;
extern HWND chkLockDrag;
extern HWND chkClipArea;
extern HWND chkPenClick;
extern HWND chkAntiAlias;
extern HWND chkDebugMode;
extern HWND chkExtrapolate;
extern HWND lblExtraTau, btnExtraDec, btnExtraVal, btnExtraInc;
extern HWND lblPalette, hCboPalette, btnPickPalette, btnSavePalette, btnDelPalette;
extern HWND btnSetBg, btnClearBg, chkCopyBgImage, chkDeleteOldBg, lblOpacity, btnOpacityDec, btnOpacityVal, btnOpacityInc;
extern HWND lblBlur, btnBlurDec, btnBlurVal, btnBlurInc;
extern HWND lblRatioPrec, btnRatioPrecDec, btnRatioPrecVal, btnRatioPrecInc;
extern HWND lblSpin, btnSpinDec, btnSpinVal, btnSpinInc;
extern HWND btnReset;

extern bool g_backingDirty;
extern double g_uiScale;
extern NOTIFYICONDATAA g_nid;

// Preset & Palette helpers
void RefreshPresetCombo();
void RefreshPaletteCombo();
void UpdateOpacityDisplay();
void UpdateBlurDisplay();
void UpdateExtrapolateDisplay();
void UpdateRatioPrecisionDisplay();
void UpdateSpinDisplay();
void ApplyBgBlur();
void LoadBackgroundImage(const std::string& path);
void SwitchTab(int tabIndex);

// Area & Coordinate updates
void UpdateValues();
void UpdateStatusText() noexcept;
void AutoMatchRatio();
void AutoMatchAreaRatio();
void ResetToFullArea();

// Window & App lifecycle
void UpdateAppFonts(int dpi);
void RelayoutUI(HWND hwnd);
void RestoreWindow(HWND hwnd);
void ShutdownDriver();
bool InitUI(HINSTANCE hInst, int nCmdShow);
int RunUIMessageLoop();
