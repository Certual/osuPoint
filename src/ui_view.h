#pragma once
#include "common.h"

extern HWND g_hwnd;
extern bool g_guiReady;
extern bool g_isUpdatingUI;

extern HWND btnTabMain, btnTabSettings, btnTabMisc;
extern HWND btnSubTune, btnSubLook, btnSubTheme;
extern HWND lblMon, hCboMon;
extern HWND lblPreset, hCboPreset, btnSavePreset, btnDelPreset;
extern HWND lblW, hW, lblH, hH, lblCX, hCX, lblCY, hCY, lblRot, hRot;
extern HWND chkRatio, hRatio, btnAutoRatio, btnArea;
extern HWND chkLockDrag;
extern HWND chkClipArea;
extern HWND chkPenClick;
extern HWND chkAntiAlias;
extern HWND chkDebugMode;
extern HWND chkOutline;
extern HWND btnOutlineOpacity;
extern HWND btnAreaOpacity;
extern HWND chkUnlockLimits;
extern HWND lblStyle, hCboStyle;
extern HWND lblBlurType, hCboBlurType;
extern HWND chkExtrapolate;
extern HWND lblExtraLead, btnExtraDec, btnExtraVal, btnExtraInc;
extern HWND lblAntiNoise, btnAntiNoiseVal;
extern HWND lblMotionFilter, btnMotionFilterVal;
extern HWND lblPalette, hCboPalette, btnPickPalette, btnSavePalette, btnDelPalette;
extern HWND lblBaseTheme, hCboBaseTheme;
extern HWND btnSetBg, btnClearBg, chkCopyBgImage, chkDeleteOldBg, lblOpacity, btnOpacityDec, btnOpacityVal, btnOpacityInc;
extern HWND lblBlur, btnBlurDec, btnBlurVal, btnBlurInc;
extern HWND lblRatioPrec, btnRatioPrecDec, btnRatioPrecVal, btnRatioPrecInc;
extern HWND lblSpin, btnSpinDec, btnSpinVal, btnSpinInc;
extern HWND btnReset, btnHelp;
extern HWND g_hHelpWnd;

extern bool g_backingDirty;
extern double g_uiScale;
extern NOTIFYICONDATAA g_nid;

// Preset & Palette helpers
void RefreshPresetCombo();
void RefreshPaletteCombo();
void UpdateOpacityDisplay();
void UpdateBlurDisplay();
void UpdateExtrapolateDisplay();
void UpdateAntiNoiseDisplay();
void UpdateMotionFilterDisplay();
void UpdateRatioPrecisionDisplay();
void UpdateSpinDisplay();
void ApplyBgBlur();
void LoadBackgroundImage(const std::string& path);
void SwitchTab(int tabIndex);
void SwitchSubTab(int subTabIndex);
void ShowHelpManual(HWND hParent);
void SetAcrylicBlur(HWND hwnd, bool enable, bool isDark);
LRESULT CALLBACK DragValSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK HelpWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

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
