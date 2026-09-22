#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <thread>
#include <atomic>
#include <algorithm>
#include <string>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

constexpr double PI = 3.14159265358979323846;
constexpr UINT WM_TRAYICON = WM_USER + 1;
constexpr UINT IDM_TRAY_OPEN = 2001;
constexpr UINT IDM_TRAY_EXIT = 2002;

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
} g_spec;

double g_inv_scale_x = 152.0 / 15200.0;
double g_inv_scale_y = 95.0 / 9500.0;

struct DriverConfig {
    std::atomic<double> width_mm{ 152.0 };
    std::atomic<double> height_mm{ 95.0 };
    std::atomic<double> center_x_mm{ 76.0 };
    std::atomic<double> center_y_mm{ 47.5 };
    std::atomic<double> rotation_deg{ 0.0 };

    std::atomic<double> cos_val{ 1.0 };
    std::atomic<double> sin_val{ 0.0 };
} g_cfg;

std::atomic<bool> g_running{ true };
HANDLE g_hDevice = INVALID_HANDLE_VALUE;
HWND g_hwnd = NULL;
bool g_guiReady = false;
std::string g_baseDir = "";

HFONT g_hFont = NULL;
HBRUSH g_hBrushBg = NULL;
HBRUSH g_hBrushEdit = NULL;
NOTIFYICONDATAA g_nid = { 0 };

std::string GetExeDirectory() {
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    return std::string(exePath);
}

USHORT ParseHexOrDec(const std::string& str) {
    if (str.rfind("0x", 0) == 0 || str.rfind("0X", 0) == 0) {
        return static_cast<USHORT>(std::strtoul(str.c_str(), nullptr, 16));
    }
    return static_cast<USHORT>(std::atoi(str.c_str()));
}

bool LoadTabletProfile(const std::string& filepath, TabletSpec& spec) {
    std::ifstream f(filepath);
    if (!f.is_open()) return false;

    std::string line;
    spec.init_feature.clear();

    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

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
            std::stringstream ss(val);
            std::string byteStr;
            while (ss >> byteStr) {
                spec.init_feature.push_back(static_cast<BYTE>(ParseHexOrDec(byteStr)));
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
        auto detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA_A)malloc(reqSize);
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
                    g_inv_scale_x = g_spec.phys_w / static_cast<double>(g_spec.max_x);
                    g_inv_scale_y = g_spec.phys_h / static_cast<double>(g_spec.max_y);
                    FindClose(hFind);
                    return devPath;
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }

    LoadTabletProfile(tabletsDir + "\\Wacom_CTL-472.cfg", g_spec);
    g_inv_scale_x = g_spec.phys_w / static_cast<double>(g_spec.max_x);
    g_inv_scale_y = g_spec.phys_h / static_cast<double>(g_spec.max_y);
    return GetDevicePath(g_spec.vid, g_spec.pid, g_spec.report_len);
}

void SaveConfig() {
    if (!g_guiReady) return;
    std::ofstream f(g_baseDir + "config.ini");
    if (!f.is_open()) return;

    f << "width=" << g_cfg.width_mm.load() << "\n";
    f << "height=" << g_cfg.height_mm.load() << "\n";
    f << "center_x=" << g_cfg.center_x_mm.load() << "\n";
    f << "center_y=" << g_cfg.center_y_mm.load() << "\n";
    f << "rotation=" << g_cfg.rotation_deg.load() << "\n";
}

void SetDefaults() {
    g_cfg.width_mm = g_spec.phys_w;
    g_cfg.height_mm = g_spec.phys_h;
    g_cfg.center_x_mm = g_spec.phys_w * 0.5;
    g_cfg.center_y_mm = g_spec.phys_h * 0.5;
    g_cfg.rotation_deg = 0.0;
    g_cfg.cos_val = 1.0;
    g_cfg.sin_val = 0.0;
}

void LoadConfig() {
    std::ifstream f(g_baseDir + "config.ini");
    if (!f.is_open()) {
        SetDefaults();
        return;
    }

    std::string line;
    double w = 0, h = 0, cx = 0, cy = 0, rot = 0;
    bool has_w = false, has_h = false, has_cx = false, has_cy = false;

    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        double val = atof(line.substr(eq + 1).c_str());

        if (key == "width") { w = val; has_w = true; }
        else if (key == "height") { h = val; has_h = true; }
        else if (key == "center_x") { cx = val; has_cx = true; }
        else if (key == "center_y") { cy = val; has_cy = true; }
        else if (key == "rotation") { rot = val; }
    }

    if (!has_w || !has_h || !has_cx || !has_cy || w <= 0 || h <= 0 || cx <= 0 || cy <= 0) {
        SetDefaults();
    } else {
        g_cfg.width_mm = w;
        g_cfg.height_mm = h;
        g_cfg.center_x_mm = cx;
        g_cfg.center_y_mm = cy;
        g_cfg.rotation_deg = rot;
        double rad = (-rot) * (PI / 180.0);
        g_cfg.cos_val = std::cos(rad);
        g_cfg.sin_val = std::sin(rad);
    }
}

void DriverThread() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    SetThreadAffinityMask(GetCurrentThread(), (1ULL << 2));

    std::string devPath = DetectAndInitTablet();
    if (devPath.empty()) {
        MessageBoxA(NULL, "Tablet not found! Please check the USB connection.", "osu!Point", MB_ICONERROR);
        return;
    }

    if (g_hwnd) {
        std::string title = "osu!Point - [" + g_spec.name + "]";
        SetWindowTextA(g_hwnd, title.c_str());

        strncpy_s(g_nid.szTip, title.c_str(), sizeof(g_nid.szTip) - 1);
        Shell_NotifyIconA(NIM_MODIFY, &g_nid);
    }

    g_hDevice = CreateFileA(devPath.c_str(), GENERIC_READ | GENERIC_WRITE, 
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);

    if (g_hDevice == INVALID_HANDLE_VALUE) {
        g_hDevice = CreateFileA(devPath.c_str(), GENERIC_READ, 
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }

    if (g_hDevice == INVALID_HANDLE_VALUE) {
        MessageBoxA(NULL, "Device handle is busy or locked by another process!", "osu!Point", MB_ICONERROR);
        return;
    }

    if (!g_spec.init_feature.empty()) {
        HidD_SetFeature(g_hDevice, g_spec.init_feature.data(), static_cast<ULONG>(g_spec.init_feature.size()));
    }

    BYTE report[64];
    DWORD bytesRead = 0;

    const int xo = g_spec.x_offset;
    const int yo = g_spec.y_offset;
    const int req_id = g_spec.report_id;
    const int max_x = g_spec.max_x;
    const int max_y = g_spec.max_y;

    INPUT mouseInput = { 0 };
    mouseInput.type = INPUT_MOUSE;
    mouseInput.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;

    while (g_running) {
        if (ReadFile(g_hDevice, report, sizeof(report), &bytesRead, nullptr) && bytesRead > 0) {
            int raw_x = 0, raw_y = 0;

            if (req_id == 0 || report[0] == req_id || report[0] == 0x10) {
                raw_x = (report[xo + 1] << 8) | report[xo];
                raw_y = (report[yo + 1] << 8) | report[yo];
            } else if (bytesRead >= 6) {
                raw_x = (report[xo] << 8) | report[xo - 1];
                raw_y = (report[yo] << 8) | report[yo - 1];
            }

            if (raw_x == 0 && raw_y == 0) continue;
            if (raw_x > max_x || raw_y > max_y) continue;

            double px = static_cast<double>(raw_x) * g_inv_scale_x;
            double py = static_cast<double>(raw_y) * g_inv_scale_y;

            double cx = g_cfg.center_x_mm.load(std::memory_order_relaxed);
            double cy = g_cfg.center_y_mm.load(std::memory_order_relaxed);
            double w  = g_cfg.width_mm.load(std::memory_order_relaxed);
            double h  = g_cfg.height_mm.load(std::memory_order_relaxed);
            double cos_r = g_cfg.cos_val.load(std::memory_order_relaxed);
            double sin_r = g_cfg.sin_val.load(std::memory_order_relaxed);

            double dx = px - cx;
            double dy = py - cy;
            double lx = dx * cos_r - dy * sin_r;
            double ly = dx * sin_r + dy * cos_r;

            double half_w = w * 0.5;
            double half_h = h * 0.5;

            double cl_x = (std::max)(-half_w, (std::min)(half_w, lx));
            double cl_y = (std::max)(-half_h, (std::min)(half_h, ly));

            mouseInput.mi.dx = static_cast<LONG>(((cl_x + half_w) / w) * 65535.0);
            mouseInput.mi.dy = static_cast<LONG>(((cl_y + half_h) / h) * 65535.0);

            SendInput(1, &mouseInput, sizeof(INPUT));
        }
    }
}

double ParseInput(HWND hEdit) {
    char buf[32];
    GetWindowTextA(hEdit, buf, 32);
    for (int i = 0; buf[i]; ++i) {
        if (buf[i] == ',') buf[i] = '.';
    }
    return atof(buf);
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

HWND hW, hH, hCX, hCY, hRot;

void UpdateValues() {
    if (!g_guiReady) return;

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

    double inv_rad = (-rot_deg) * (PI / 180.0);
    g_cfg.cos_val = std::cos(inv_rad);
    g_cfg.sin_val = std::sin(inv_rad);

    g_cfg.width_mm = w;
    g_cfg.height_mm = h;
    g_cfg.center_x_mm = cx;
    g_cfg.center_y_mm = cy;
    g_cfg.rotation_deg = rot_deg;

    SaveConfig();
    if (g_hwnd) InvalidateRect(g_hwnd, NULL, FALSE);
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
    HBITMAP hbmColor = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, (void**)&pPixels, NULL, 0);
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

void DrawPreview(HDC hdc) {
    const int box_x = 225, box_y = 15;
    const int max_draw_w = 210;
    const int max_draw_h = 138;
    const int BEZEL = 7;

    int active_max_w = max_draw_w - 2 * BEZEL;
    int active_max_h = max_draw_h - 2 * BEZEL;

    double scale = (std::min)(static_cast<double>(active_max_w) / g_spec.phys_w, 
                              static_cast<double>(active_max_h) / g_spec.phys_h);

    int active_w = static_cast<int>(std::round(g_spec.phys_w * scale));
    int active_h = static_cast<int>(std::round(g_spec.phys_h * scale));

    int tab_w = active_w + 2 * BEZEL;
    int tab_h = active_h + 2 * BEZEL;

    HBRUSH cardBrush = CreateSolidBrush(COLOR_CARD);
    HPEN borderPen = CreatePen(PS_SOLID, 1, COLOR_BORDER);
    HGDIOBJ oldBrush = SelectObject(hdc, cardBrush);
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    RoundRect(hdc, box_x - 5, box_y - 5, box_x + tab_w + 5, box_y + tab_h + 5, 8, 8);

    HBRUSH tabBrush = CreateSolidBrush(RGB(34, 34, 42));
    SelectObject(hdc, tabBrush);
    RoundRect(hdc, box_x, box_y, box_x + tab_w, box_y + tab_h, 6, 6);
    DeleteObject(tabBrush);

    int origin_x = box_x + BEZEL;
    int origin_y = box_y + BEZEL;

    double cx = g_cfg.center_x_mm.load(std::memory_order_relaxed);
    double cy = g_cfg.center_y_mm.load(std::memory_order_relaxed);
    double w  = g_cfg.width_mm.load(std::memory_order_relaxed);
    double h  = g_cfg.height_mm.load(std::memory_order_relaxed);
    double rot_deg = g_cfg.rotation_deg.load(std::memory_order_relaxed);
    double rad = rot_deg * (PI / 180.0);

    double c = std::cos(rad);
    double s = std::sin(rad);

    double hw = w * 0.5;
    double hh = h * 0.5;

    double local_corners[4][2] = {
        { -hw, -hh },
        {  hw, -hh },
        {  hw,  hh },
        { -hw,  hh }
    };

    POINT pts[4];
    for (int i = 0; i < 4; ++i) {
        double rx = cx + (local_corners[i][0] * c - local_corners[i][1] * s);
        double ry = cy + (local_corners[i][0] * s + local_corners[i][1] * c);
        pts[i].x = origin_x + static_cast<int>(std::round(rx * scale));
        pts[i].y = origin_y + static_cast<int>(std::round(ry * scale));
    }

    HRGN clipRgn = CreateRectRgn(origin_x - 2, origin_y - 2, origin_x + active_w + 3, origin_y + active_h + 3);
    SelectClipRgn(hdc, clipRgn);

    HBRUSH areaBrush = CreateSolidBrush(COLOR_ACCENT);
    HPEN areaPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
    SelectObject(hdc, areaBrush);
    SelectObject(hdc, areaPen);

    Polygon(hdc, pts, 4);

    int center_px_x = origin_x + static_cast<int>(std::round(cx * scale));
    int center_px_y = origin_y + static_cast<int>(std::round(cy * scale));
    HBRUSH dotBrush = CreateSolidBrush(RGB(255, 255, 255));
    SelectObject(hdc, dotBrush);
    SelectObject(hdc, GetStockObject(NULL_PEN));
    Ellipse(hdc, center_px_x - 2, center_px_y - 2, center_px_x + 3, center_px_y + 3);
    DeleteObject(dotBrush);

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
    SetWindowTextA(hW, FormatDouble(g_spec.phys_w).c_str());
    SetWindowTextA(hH, FormatDouble(g_spec.phys_h).c_str());
    SetWindowTextA(hCX, FormatDouble(g_spec.phys_w * 0.5).c_str());
    SetWindowTextA(hCY, FormatDouble(g_spec.phys_h * 0.5).c_str());
    SetWindowTextA(hRot, "0");
    UpdateValues();
}

void RestoreWindow(HWND hwnd) {
    ShowWindow(hwnd, SW_SHOW);
    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
}

void KillProcessNow() {
    g_running = false;
    timeEndPeriod(1);
    SaveConfig();
    
    if (g_nid.hWnd) {
        Shell_NotifyIconA(NIM_DELETE, &g_nid);
        g_nid.hWnd = NULL;
    }

    if (g_hDevice != INVALID_HANDLE_VALUE) {
        CancelIoEx(g_hDevice, nullptr);
        CloseHandle(g_hDevice);
        g_hDevice = INVALID_HANDLE_VALUE;
    }
    TerminateProcess(GetCurrentProcess(), 0);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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

    if (msg == WM_COMMAND) {
        if (HIWORD(wParam) == EN_CHANGE && g_guiReady) {
            UpdateValues();
        }
        if (LOWORD(wParam) == 1001) {
            ResetToFullArea();
        }
        if (LOWORD(wParam) == IDM_TRAY_OPEN) {
            RestoreWindow(hwnd);
        }
        if (LOWORD(wParam) == IDM_TRAY_EXIT) {
            KillProcessNow();
        }
    }
    if (msg == WM_CTLCOLOREDIT) {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, COLOR_TEXT);
        SetBkColor(hdc, COLOR_EDIT_BG);
        return (LRESULT)g_hBrushEdit;
    }
    if (msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORBTN) {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, COLOR_TEXT_MUTED);
        SetBkColor(hdc, COLOR_BG);
        return (LRESULT)g_hBrushBg;
    }
    if (msg == WM_DRAWITEM) {
        LPDRAWITEMSTRUCT pDIS = (LPDRAWITEMSTRUCT)lParam;
        if (pDIS->CtlID == 1001) {
            FillRect(pDIS->hDC, &pDIS->rcItem, g_hBrushBg);

            bool pressed = (pDIS->itemState & ODS_SELECTED);
            HBRUSH btnBrush = CreateSolidBrush(pressed ? COLOR_BTN_HOVER : COLOR_BTN);
            HPEN btnPen = CreatePen(PS_SOLID, 1, COLOR_BORDER);
            HGDIOBJ ob = SelectObject(pDIS->hDC, btnBrush);
            HGDIOBJ op = SelectObject(pDIS->hDC, btnPen);

            RoundRect(pDIS->hDC, pDIS->rcItem.left, pDIS->rcItem.top, pDIS->rcItem.right, pDIS->rcItem.bottom, 6, 6);

            SetBkMode(pDIS->hDC, TRANSPARENT);
            SetTextColor(pDIS->hDC, COLOR_TEXT);
            SelectObject(pDIS->hDC, g_hFont);
            DrawTextA(pDIS->hDC, "Full Area (Reset)", -1, &pDIS->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

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
        DrawPreview(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_CLOSE || msg == WM_DESTROY) {
        KillProcessNow();
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    timeBeginPeriod(1);
    SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);

    g_baseDir = GetExeDirectory();
    DetectAndInitTablet();
    LoadConfig();

    g_hBrushBg = CreateSolidBrush(COLOR_BG);
    g_hBrushEdit = CreateSolidBrush(COLOR_EDIT_BG);
    g_hFont = CreateFontA(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hbrBackground = g_hBrushBg;
    wc.lpszClassName = "osuPoint_Driver";
    RegisterClassA(&wc);

    std::string title = "osu!Point - [" + g_spec.name + "]";
    g_hwnd = CreateWindowA("osuPoint_Driver", title.c_str(),
        WS_OVERLAPPEDWINDOW ^ (WS_THICKFRAME | WS_MAXIMIZEBOX), 
        CW_USEDEFAULT, CW_USEDEFAULT, 470, 240, NULL, NULL, hInst, NULL);

    BOOL useDarkMode = TRUE;
    if (FAILED(DwmSetWindowAttribute(g_hwnd, 20, &useDarkMode, sizeof(useDarkMode)))) {
        DwmSetWindowAttribute(g_hwnd, 19, &useDarkMode, sizeof(useDarkMode));
    }

    HICON hIcon = CreateAppIcon();
    if (hIcon) {
        SendMessage(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        SendMessage(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

        // Register System Tray Icon
        g_nid.cbSize = sizeof(NOTIFYICONDATAA);
        g_nid.hWnd = g_hwnd;
        g_nid.uID = 1;
        g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAYICON;
        g_nid.hIcon = hIcon;
        strncpy_s(g_nid.szTip, title.c_str(), sizeof(g_nid.szTip) - 1);
        Shell_NotifyIconA(NIM_ADD, &g_nid);
    }

    HWND lblW = CreateWindowA("STATIC", "Width (mm):", WS_VISIBLE | WS_CHILD, 15, 15, 105, 20, g_hwnd, NULL, NULL, NULL);
    hW = CreateWindowA("EDIT", FormatDouble(g_cfg.width_mm.load()).c_str(), WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 125, 14, 80, 22, g_hwnd, NULL, NULL, NULL);

    HWND lblH = CreateWindowA("STATIC", "Height (mm):", WS_VISIBLE | WS_CHILD, 15, 45, 105, 20, g_hwnd, NULL, NULL, NULL);
    hH = CreateWindowA("EDIT", FormatDouble(g_cfg.height_mm.load()).c_str(), WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 125, 44, 80, 22, g_hwnd, NULL, NULL, NULL);

    HWND lblCX = CreateWindowA("STATIC", "Center X (mm):", WS_VISIBLE | WS_CHILD, 15, 75, 105, 20, g_hwnd, NULL, NULL, NULL);
    hCX = CreateWindowA("EDIT", FormatDouble(g_cfg.center_x_mm.load()).c_str(), WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 125, 74, 80, 22, g_hwnd, NULL, NULL, NULL);

    HWND lblCY = CreateWindowA("STATIC", "Center Y (mm):", WS_VISIBLE | WS_CHILD, 15, 105, 105, 20, g_hwnd, NULL, NULL, NULL);
    hCY = CreateWindowA("EDIT", FormatDouble(g_cfg.center_y_mm.load()).c_str(), WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 125, 104, 80, 22, g_hwnd, NULL, NULL, NULL);

    HWND lblRot = CreateWindowA("STATIC", "Rotation (deg):", WS_VISIBLE | WS_CHILD, 15, 135, 105, 20, g_hwnd, NULL, NULL, NULL);
    hRot = CreateWindowA("EDIT", FormatDouble(g_cfg.rotation_deg.load()).c_str(), WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL, 125, 134, 80, 22, g_hwnd, NULL, NULL, NULL);

    HWND btnReset = CreateWindowA("BUTTON", "Full Area (Reset)", WS_VISIBLE | WS_CHILD | BS_OWNERDRAW, 15, 166, 190, 26, g_hwnd, (HMENU)1001, NULL, NULL);

    HWND uiControls[] = { lblW, hW, lblH, hH, lblCX, hCX, lblCY, hCY, lblRot, hRot, btnReset };
    for (HWND c : uiControls) {
        SendMessage(c, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    }

    g_guiReady = true;
    UpdateValues();

    ShowWindow(g_hwnd, nCmdShow);

    std::thread driver(DriverThread);
    driver.detach();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    KillProcessNow();
    return 0;
}
