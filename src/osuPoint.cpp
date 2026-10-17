#include "common.h"
#include "platform.h"
#include "tablet_decoder.h"
#include "driver_pipeline.h"
#include "config_manager.h"
#include "ui_view.h"
#include "cli.h"
#include <fstream>
#include <io.h>
#include <fcntl.h>

#pragma code_seg(".text$cold")

bool g_debugCliOverride = false;

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
    if (!SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS)) {
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    }
    DisablePowerThrottling();
    EnableSubMillisecondTimer();

    QueryPerformanceFrequency(&g_qpcFreq);
    g_qpcToUs = (g_qpcFreq.QuadPart > 0) ? (1000000.0 / static_cast<double>(g_qpcFreq.QuadPart)) : 0.0;

    g_affinityConfig = CalculateThreadAffinity();
    if (g_affinityConfig.ui != 0) {
        SetThreadAffinityMask(GetCurrentThread(), g_affinityConfig.ui);
    }

    g_baseDir = GetExeDirectory();
    RefreshMonitors();

    auto runConsoleCmd = [](auto action) {
        AttachConsoleIO();
        action();
        fflush(stdout);
        fflush(stderr);
        FreeConsole();
    };

    CliOptions cli = ParseCommandLineArgs(GetCommandLineA());
    if (cli.show_help) {
        runConsoleCmd([] {
            PrintHelp();
        });
        return cli.exit_code;
    }

    if (cli.debug) {
        g_debugMode = true;
        g_debugCliOverride = true;
        SetupDebugConsole(true);
    }

    if (!cli.dump_reports_file.empty()) {
        g_dumpReportsFile = cli.dump_reports_file;
    }

    if (cli.calibrate) {
        g_calibrate = true;
        AttachConsoleIO();
    }

#if defined(ENABLE_SELFTEST) && ENABLE_SELFTEST
    if (cli.selftest) {
        runConsoleCmd([] {
            extern void RunSelfTest();
            RunSelfTest();
        });
        return 0;
    }
#endif

    if (cli.probe) {
        RunDiagnosticProbe();
        return 0;
    }

    if (cli.list_devices) {
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
