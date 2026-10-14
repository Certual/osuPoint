#include "common.h"
#include "platform.h"
#include "tablet_decoder.h"
#include "driver_pipeline.h"
#include "config_manager.h"
#include "ui_view.h"
#include <fstream>
#include <io.h>
#include <fcntl.h>

#pragma code_seg(".text$cold")

bool g_debugCliOverride = false;

static void AttachConsoleIO() {
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
        return;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* fp = nullptr;
        freopen_s(&fp, "CONOUT$", "w", stdout);
        freopen_s(&fp, "CONOUT$", "w", stderr);
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
    }
}

static void RunDiagnosticProbe() {
    AttachConsoleIO();
    printf("[PROBE] osu!Point Diagnostic Probe\n");
    printf("[PROBE] Enumerating HID devices...\n");
    std::vector<std::string> devPaths = DetectAndInitTablet(false);
    if (devPaths.empty()) {
        printf("[PROBE] No tablet devices detected.\n");
        fflush(stdout);
        fflush(stderr);
        FreeConsole();
        return;
    }
    printf("[PROBE] Selected: %s (VID: 0x%04X, PID: 0x%04X)\n", g_spec.name.c_str(), g_spec.vid, g_spec.pid);
    printf("[PROBE] Area: max=(%d, %d), phys=(%.2f, %.2f mm)\n", g_spec.max_x, g_spec.max_y, g_spec.phys_w, g_spec.phys_h);
    printf("[PROBE] Offsets: x=%d, y=%d, bits=%d, report_id=0x%02X\n", g_spec.x_offset, g_spec.y_offset, g_spec.coord_bits, g_spec.report_id);

    HANDLE hDev = OpenTabletHandle(devPaths[0]);
    if (hDev == INVALID_HANDLE_VALUE) {
        printf("[PROBE] Failed to open tablet handle.\n");
        fflush(stdout);
        fflush(stderr);
        FreeConsole();
        return;
    }

    PHIDP_PREPARSED_DATA ppData = nullptr;
    if (HidD_GetPreparsedData(hDev, &ppData)) {
        HIDP_CAPS caps{};
        if (HidP_GetCaps(ppData, &caps) == HIDP_STATUS_SUCCESS) {
            printf("[PROBE] HID Descriptor Caps:\n");
            printf("        UsagePage: 0x%04X, Usage: 0x%04X\n", caps.UsagePage, caps.Usage);
            printf("        InputReportByteLength: %u\n", caps.InputReportByteLength);
            printf("        OutputReportByteLength: %u\n", caps.OutputReportByteLength);
            printf("        FeatureReportByteLength: %u\n", caps.FeatureReportByteLength);
            printf("        NumberInputValueCaps: %u\n", caps.NumberInputValueCaps);
            printf("        NumberInputButtonCaps: %u\n", caps.NumberInputButtonCaps);
        }
        HidD_FreePreparsedData(ppData);
    }

    printf("[PROBE] Capturing up to 50 raw packets (bring pen near tablet, 5s timeout)...\n");
    std::ofstream probeFile("probe_dump.txt");
    if (probeFile.is_open()) {
        probeFile << "# osu!Point Probe Report\n";
        probeFile << "# Device: " << g_spec.name << "\n";
        probeFile << "# VID=0x" << std::hex << g_spec.vid << " PID=0x" << g_spec.pid << std::dec << "\n";
    }

    OVERLAPPED ov{};
    ov.hEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    BYTE buf[256]{};
    int collected = 0;
    ULONGLONG startMs = GetTickCount64();

    while (collected < 50 && (GetTickCount64() - startMs) < 5000) {
        ov.Internal = 0;
        ov.InternalHigh = 0;
        DWORD bytesRead = 0;
        if (!ReadFile(hDev, buf, sizeof(buf), nullptr, &ov)) {
            if (GetLastError() == ERROR_IO_PENDING) {
                if (WaitForSingleObject(ov.hEvent, 100) == WAIT_OBJECT_0) {
                    bytesRead = static_cast<DWORD>(ov.InternalHigh);
                } else {
                    CancelIo(hDev);
                    continue;
                }
            } else {
                continue;
            }
        } else {
            bytesRead = static_cast<DWORD>(ov.InternalHigh);
        }

        if (bytesRead > 0) {
            collected++;
            printf("[PROBE #%02d] len=%lu: ", collected, bytesRead);
            if (probeFile.is_open()) probeFile << "[" << collected << "] ";
            for (DWORD b = 0; b < bytesRead && b < 16; ++b) {
                printf("%02X ", buf[b]);
                if (probeFile.is_open()) probeFile << std::hex << (int)buf[b] << " ";
            }
            printf("\n");
            if (probeFile.is_open()) probeFile << "\n";
        }
    }

    if (ov.hEvent) CloseHandle(ov.hEvent);
    CloseHandle(hDev);
    if (probeFile.is_open()) probeFile.close();

    printf("[PROBE] Captured %d packets. Saved to probe_dump.txt\n", collected);
    fflush(stdout);
    fflush(stderr);
    FreeConsole();
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    InitSendInputSyscall();
    EnablePerMonitorDpi();
    EnableSubMillisecondTimer();
    if (!SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS)) {
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    }
    DisablePowerThrottling();

    QueryPerformanceFrequency(&g_qpcFreq);
    g_qpcToUs = (g_qpcFreq.QuadPart > 0) ? (1000000.0 / static_cast<double>(g_qpcFreq.QuadPart)) : 0.0;

    g_affinityConfig = CalculateThreadAffinity();
    if (g_affinityConfig.ui != 0) {
        SetThreadAffinityMask(GetCurrentThread(), g_affinityConfig.ui);
    }

    QueryPerformanceFrequency(&g_qpcFreq);
    g_qpcToUs = (g_qpcFreq.QuadPart > 0) ? (1000000.0 / static_cast<double>(g_qpcFreq.QuadPart)) : 0.0;

    g_baseDir = GetExeDirectory();
    RefreshMonitors();

    auto runConsoleCmd = [](auto action) {
        AttachConsoleIO();
        action();
        fflush(stdout);
        fflush(stderr);
        FreeConsole();
    };

    const std::string_view cmd = GetCommandLineA();
    if (cmd.find("--help") != std::string_view::npos) {
        runConsoleCmd([] {
            printf("osu!Point - Advanced Tablet Driver\n"
                   "Usage:\n"
                   "  --debug          Enable debug mode and verbose trace logs\n"
                   "  --list-devices   Show all detected devices\n"
                   "  --calibrate      Start calibration mode\n"
                   "  --dump-reports   <file> Dump raw reports to binary file\n"
#if defined(ENABLE_SELFTEST) && ENABLE_SELFTEST
                   "  --selftest       Run self tests\n"
#endif
                   "  --probe          Dump HID descriptor caps and 50 raw packets\n");
        });
        return 0;
    }

    if (cmd.find("--debug") != std::string_view::npos) {
        g_debugMode = true;
        g_debugCliOverride = true;
        SetupDebugConsole(true);
    }

    if (size_t pos = cmd.find("--dump-reports"); pos != std::string_view::npos) {
        std::string_view sub = cmd.substr(pos + 14);
        while (!sub.empty() && sub.front() == ' ') sub.remove_prefix(1);
        size_t end = sub.find(' ');
        if (!sub.empty()) {
            g_dumpReportsFile = std::string(end == std::string_view::npos ? sub : sub.substr(0, end));
        }
    }

    if (cmd.find("--calibrate") != std::string_view::npos) {
        g_calibrate = true;
        AttachConsoleIO();
    }

#if defined(ENABLE_SELFTEST) && ENABLE_SELFTEST
    if (cmd.find("--selftest") != std::string_view::npos) {
        runConsoleCmd([] {
            extern void RunSelfTest();
            RunSelfTest();
        });
        return 0;
    }
#endif

    if (cmd.find("--probe") != std::string_view::npos) {
        RunDiagnosticProbe();
        return 0;
    }

    if (cmd.find("--list-devices") != std::string_view::npos) {
        runConsoleCmd([] {
            DetectAndInitTablet(true);
        });
        return 0;
    }

    DetectAndInitTablet(false);
    LoadConfig();

    if (!InitUI(hInst, nCmdShow)) {
        return 1;
    }

    StartDriverPipeline();
    int exitCode = RunUIMessageLoop();
    ShutdownDriver();

    return exitCode;
}
