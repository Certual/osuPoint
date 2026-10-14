#pragma code_seg(".text$hot")
#include "driver_pipeline.h"
#include "config_manager.h"
#include "platform.h"
#include "ui_view.h"

TabletSpec g_spec;
AtomicConfigStore g_cfgStore;
LatencyStats g_latencyStats;
JitterStats g_jitterStats;
WacomDiagnosticStats g_wacomStats;
LivePenTelemetry g_livePen{};

LARGE_INTEGER g_qpcFreq{ 0 };
double g_qpcToUs = 0.0;

alignas(64) std::atomic<uint64_t> g_reportsReceived{ 0 };
alignas(64) std::atomic<uint64_t> g_rawReportsReceived{ 0 };
alignas(64) std::atomic<uint64_t> g_reportsDeduped{ 0 };
alignas(64) std::atomic<uint32_t> g_reconnectCount{ 0 };
alignas(64) std::atomic<DWORD>    g_lastReadError{ 0 };
alignas(64) std::atomic<uint32_t> g_lastReportBytes{ 0 };
alignas(64) std::atomic<int>      g_numActiveEndpoints{ 0 };
alignas(64) std::atomic<uint64_t> g_epCount[4]{ 0 };

std::atomic<HANDLE> g_hDevice{ INVALID_HANDLE_VALUE };
std::atomic<bool> g_sharedModeFallback{ false };
HANDLE g_hDeviceChangeEvent = NULL;
HDEVNOTIFY g_hDevNotify = NULL;

std::jthread g_readerThread;
std::jthread g_dispatchThread;

std::atomic<bool> g_penClick{ true };
std::atomic<bool> g_barrelClick{ true };
std::string g_dumpReportsFile = "";
bool g_calibrate = false;
bool g_clipArea = false;
bool g_debugMode = false;

static SpscRing<RawPacket, 64> g_packetRing;
static HANDLE g_hPacketReady = NULL;
alignas(64) static std::atomic<bool> g_dispatcherWaiting{ false };

std::atomic<bool> g_removalPending{false};
char g_removedPath[260]{0};


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

    cfg.extrapolation = g_extrapolation;
    cfg.extrapolation_tau = g_extrapolationTau;
    cfg.spin_percent = g_spinPercent;

    g_cfgStore.Store(cfg);
}

std::vector<std::string> DetectAndInitTablet(bool list_devices) {
    std::vector<std::string> resultPaths;
    InitAllTabletSpecs();
    const auto& allSpecs = g_cachedSpecs;

    struct DeviceCandidate {
        std::string path;
        uint16_t vid;
        uint16_t pid;
        std::string product;
        HIDP_CAPS caps;
        int mi_index;
        bool can_open;
    };

    std::vector<DeviceCandidate> candidates;
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO devInfo = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo != INVALID_HANDLE_VALUE) {
        SP_DEVICE_INTERFACE_DATA devData{ sizeof(SP_DEVICE_INTERFACE_DATA) };
        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &devData); ++i) {
            DWORD reqSize = 0;
            SetupDiGetDeviceInterfaceDetailW(devInfo, &devData, nullptr, 0, &reqSize, nullptr);
            if (reqSize == 0) continue;
            std::vector<BYTE> detailBuf(reqSize);
            auto detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detailBuf.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (SetupDiGetDeviceInterfaceDetailW(devInfo, &devData, detail, reqSize, nullptr, nullptr)) {
                HANDLE h = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
                if (h != INVALID_HANDLE_VALUE) {
                    HIDD_ATTRIBUTES attr{ sizeof(HIDD_ATTRIBUTES) };
                    if (HidD_GetAttributes(h, &attr)) {
                        DeviceCandidate c;
                        c.path = Utf16ToUtf8(detail->DevicePath);
                        c.vid = attr.VendorID;
                        c.pid = attr.ProductID;
                        
                        wchar_t wProd[128] = {};
                        if (HidD_GetProductString(h, wProd, sizeof(wProd))) {
                            char mbs[128] = {};
                            WideCharToMultiByte(CP_UTF8, 0, wProd, -1, mbs, sizeof(mbs), nullptr, nullptr);
                            c.product = mbs;
                        }

                        PHIDP_PREPARSED_DATA ppData = nullptr;
                        if (HidD_GetPreparsedData(h, &ppData)) {
                            if (HidP_GetCaps(ppData, &c.caps) != HIDP_STATUS_SUCCESS) {
                                memset(&c.caps, 0, sizeof(c.caps));
                            }
                            HidD_FreePreparsedData(ppData);
                        } else {
                            memset(&c.caps, 0, sizeof(c.caps));
                        }
                        
                        c.mi_index = -1;
                        const wchar_t* mi_str = wcsstr(detail->DevicePath, L"&mi_");
                        if (!mi_str) mi_str = wcsstr(detail->DevicePath, L"&MI_");
                        if (mi_str) {
                            c.mi_index = _wtoi(mi_str + 4);
                        }

                        HANDLE hTestOpen = CreateFileW(detail->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
                        c.can_open = (hTestOpen != INVALID_HANDLE_VALUE);
                        if (hTestOpen != INVALID_HANDLE_VALUE) {
                            CloseHandle(hTestOpen);
                        }

                        candidates.push_back(c);
                    }
                    CloseHandle(h);
                }
            }
        }
        SetupDiDestroyDeviceInfoList(devInfo);
    }
    
    int bestTotal = -999999;
    TabletSpec bestSpec;
    std::string bestPath;
    
    auto nameMatchScore = [](const std::string& hidProd, const std::string& dbName) noexcept -> int {
        if (hidProd.empty() || dbName.empty()) return 0;
        auto isAlNum = [](char c) noexcept -> bool { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); };
        auto toLow = [](char c) noexcept -> char { return (c >= 'A' && c <= 'Z') ? char(c + 32) : c; };

        char h[128] = {}, n[128] = {};
        for (size_t i = 0; i < hidProd.size() && i < 127; ++i) h[i] = toLow(hidProd[i]);
        for (size_t i = 0; i < dbName.size() && i < 127; ++i) n[i] = toLow(dbName[i]);

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

    for (const auto& c : candidates) {
        for (const auto& spec : allSpecs) {
            if (c.vid == spec.vid && c.pid == spec.pid) {
                int totalScore = 0;
                totalScore += nameMatchScore(c.product, spec.name);

                if (c.caps.UsagePage == 0x0D && c.caps.Usage == 0x02) totalScore += 1000;
                else if (c.caps.UsagePage == 0x0D && c.caps.Usage == 0x01) totalScore += 800;
                else if (c.caps.UsagePage == 0x0D) totalScore += 600;
                else if (c.caps.UsagePage >= 0xFF00) totalScore += 400;
                else if (c.caps.UsagePage == 0x01 && c.caps.Usage == 0x02) totalScore += 100;

                if (c.caps.InputReportByteLength == spec.report_len) totalScore += 500;
                else if (c.caps.InputReportByteLength > 0 && spec.report_len > 0) {
                    int diff = abs((int)c.caps.InputReportByteLength - spec.report_len);
                    totalScore += (std::max)(0, 200 - diff * 20);
                }

                if (c.mi_index == 0) totalScore += 50;
                else if (c.mi_index > 0) totalScore -= c.mi_index * 10;

                if (c.can_open) totalScore += 200;

                if (totalScore > bestTotal) {
                    bestTotal = totalScore;
                    bestSpec = spec;
                    bestPath = c.path;
                }
            }
        }
    }

    if (list_devices) {
        if (!bestPath.empty()) {
            char buf[512];
            snprintf(buf, sizeof(buf), "Selected Tablet: %s (VID: 0x%04X, PID: 0x%04X, MatchScore: %d)\nPath: %s",
                bestSpec.name.c_str(), bestSpec.vid, bestSpec.pid, bestTotal, bestPath.c_str());
            printf("%s\n", buf);
        } else {
            printf("No matching devices found.\n");
        }
        return {};
    }

    if (!bestPath.empty()) {
        uint16_t prevThreshold = g_spec.tip_pressure_threshold;
        g_spec = bestSpec;
        if (prevThreshold > 0 && g_spec.tip_pressure_threshold == 0) {
            g_spec.tip_pressure_threshold = prevThreshold;
        }
        if (g_debugMode) {
            InitPinnedConsoleHeader();
            ConsoleLock();
            printf("[DEBUG] Selected tablet: %s (VID: 0x%04X, PID: 0x%04X, Score: %d)\n",
                   g_spec.name.c_str(), g_spec.vid, g_spec.pid, bestTotal);
            printf("[DEBUG] Physical area: (%.2f x %.2f mm), Max coords: (%d, %d), Bits: %d, ReportID: 0x%02X\n",
                   g_spec.phys_w, g_spec.phys_h, g_spec.max_x, g_spec.max_y, g_spec.coord_bits, g_spec.report_id);
            printf("[DEBUG] Primary path: %s\n", bestPath.c_str());
            fflush(stdout);
            ConsoleUnlock();
        }
        resultPaths.push_back(bestPath);
        if (!VendorID::IsWacom(bestSpec.vid)) {
            for (const auto& c : candidates) {
                if (c.path != bestPath && c.vid == bestSpec.vid && c.pid == bestSpec.pid && c.can_open) {
                    if (c.caps.UsagePage >= 0xFF00 || c.caps.UsagePage == 0x0D) {
                        if (resultPaths.size() < 4) {
                            resultPaths.push_back(c.path);
                        }
                    }
                }
            }
        }
    }
    return resultPaths;
}

HANDLE OpenTabletHandle(const std::string& devPath, bool isPrimary) noexcept {
    const std::wstring wDevPath = Utf8ToUtf16(devPath);
    constexpr struct { DWORD access; DWORD share; bool shared; } modes[] = {
        { GENERIC_READ | GENERIC_WRITE, 0, false },
        { GENERIC_READ, 0, false },
        { GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, true },
        { GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, true },
    };

    HANDLE hDevice = INVALID_HANDLE_VALUE;
    bool fellBack = false;
    for (const auto& m : modes) {
        hDevice = CreateFileW(wDevPath.c_str(), m.access, m.share, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (hDevice != INVALID_HANDLE_VALUE) {
            fellBack = m.shared;
            SetFileCompletionNotificationModes(hDevice, FILE_SKIP_SET_EVENT_ON_HANDLE);
            break;
        }
    }
    if (isPrimary) {
        g_sharedModeFallback.store(fellBack, std::memory_order_relaxed);

        if (fellBack) {
            LogMessage("WARNING: Opened tablet in Shared Mode! Native driver conflict detected.");
            if (g_hwnd) {
                PostMessageA(g_hwnd, WM_USER + 11, 0, 0);
            } else {
                g_needsConflictWarning.store(true, std::memory_order_relaxed);
            }
        }
    }

    return hDevice;
}

void ReaderThread(std::stop_token stoken) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    if (g_affinityConfig.reader != 0) {
        SetThreadAffinityMask(GetCurrentThread(), g_affinityConfig.reader);
        SetThreadIdealProcessor(GetCurrentThread(), g_affinityConfig.readerCore);
    }

    DisablePowerThrottling();
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);

    HANDLE hMmcss = nullptr;
    if (GetPriorityClass(GetCurrentProcess()) != REALTIME_PRIORITY_CLASS) {
        DWORD taskIndex = 0;
        hMmcss = AvSetMmThreadCharacteristicsA("Pro Audio", &taskIndex);
        if (hMmcss) {
            AvSetMmThreadPriority(hMmcss, AVRT_PRIORITY_CRITICAL);
        }
    }

    constexpr int MAX_ENDPOINTS = 4;
    constexpr int BUFFERS_PER_SLOT = 2;

    struct alignas(64) IoBuffer {
        OVERLAPPED ov{};
        alignas(64) BYTE buf[256]{};
        bool isPending = false;
    };

    struct alignas(64) EndpointSlot {
        HANDLE hDevice = INVALID_HANDLE_VALUE;
        int inputReportLen = 0;
        int outputReportLen = 0;
        int featureReportLen = 0;
        IoBuffer io[BUFFERS_PER_SLOT];
        int nextToRead = 0;
        std::string path;
    };

    EndpointSlot slots[MAX_ENDPOINTS];
    int numSlots = 0;
    int consecutiveErrors = 0;
    uint64_t rawReportsCount = 0;

    auto closeAllSlots = [&]() noexcept {
        for (int i = 0; i < numSlots; ++i) {
            for (int b = 0; b < BUFFERS_PER_SLOT; ++b) {
                if (slots[i].hDevice != INVALID_HANDLE_VALUE && slots[i].io[b].isPending) {
                    CancelIoEx(slots[i].hDevice, &slots[i].io[b].ov);
                }
                if (slots[i].io[b].ov.hEvent) {
                    CloseHandle(slots[i].io[b].ov.hEvent);
                    slots[i].io[b].ov.hEvent = nullptr;
                }
                slots[i].io[b].isPending = false;
            }
            if (slots[i].hDevice != INVALID_HANDLE_VALUE) {
                CloseHandle(slots[i].hDevice);
                slots[i].hDevice = INVALID_HANDLE_VALUE;
            }
            slots[i].nextToRead = 0;
        }
        numSlots = 0;
        consecutiveErrors = 0;
        g_hDevice.store(INVALID_HANDLE_VALUE, std::memory_order_release);
        g_numActiveEndpoints.store(0, std::memory_order_relaxed);

        RawPacket resetPkt{};
        resetPkt.len = 0;
        resetPkt.endpointIdx = static_cast<uint32_t>(-1);
        g_packetRing.TryPush(resetPkt);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        if (g_dispatcherWaiting.load(std::memory_order_relaxed) && g_hPacketReady) {
            SetEvent(g_hPacketReady);
        }
    };

    auto sendTabletWakeHandshake = [&](HANDLE hDev, int outLen, int featLen) noexcept {
        (void)featLen;
        if (!hDev || hDev == INVALID_HANDLE_VALUE) return;

        wchar_t strDesc[256]{};
        HidD_GetIndexedString(hDev, 2, strDesc, sizeof(strDesc));
        HidD_GetIndexedString(hDev, 100, strDesc, sizeof(strDesc));
        HidD_GetIndexedString(hDev, 110, strDesc, sizeof(strDesc));
        std::memset(strDesc, 0, sizeof(strDesc));
        if (HidD_GetIndexedString(hDev, 200, strDesc, sizeof(strDesc))) {
            if (VendorID::IsHuionFamily(g_spec.vid) || VendorID::IsXPPenFamily(g_spec.vid)) {
                const auto* b = reinterpret_cast<const uint8_t*>(strDesc);
                const int32_t hwMaxX = static_cast<int32_t>(b[0] | (b[1] << 8) | (b[2] << 16));
                const int32_t hwMaxY = static_cast<int32_t>(b[3] | (b[4] << 8) | (b[5] << 16));
                const uint32_t hwLpi = static_cast<uint32_t>(b[8] | (b[9] << 8));
                if (hwMaxX >= 2000 && hwMaxX <= 250000 && hwMaxY >= 2000 && hwMaxY <= 250000 &&
                    hwLpi >= 1000 && hwLpi <= 20000) {
                    g_spec.max_x = hwMaxX;
                    g_spec.max_y = hwMaxY;
                    g_spec.phys_w = (static_cast<double>(hwMaxX) / static_cast<double>(hwLpi)) * 25.4;
                    g_spec.phys_h = (static_cast<double>(hwMaxY) / static_cast<double>(hwLpi)) * 25.4;
                    g_spec.coord_bits = (hwMaxX > 0x1FFFF || hwMaxY > 0x1FFFF) ? 18 :
                                        ((hwMaxX > 0xFFFF || hwMaxY > 0xFFFF) ? 17 : 16);
                }
            }
        }
        HidD_GetIndexedString(hDev, 201, strDesc, sizeof(strDesc));

        auto writeOverlapped = [&](const void* ptr, DWORD len) {
            OVERLAPPED ovW{};
            ovW.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
            if (ovW.hEvent) {
                DWORD written = 0;
                if (!WriteFile(hDev, ptr, len, &written, &ovW)) {
                    if (GetLastError() == ERROR_IO_PENDING) {
                        GetOverlappedResult(hDev, &ovW, &written, TRUE);
                    }
                }
                CloseHandle(ovW.hEvent);
            }
        };

        for (const auto& step : g_spec.init_steps) {
            if (step.type == InitStepType::SetFeature) {
                HidD_SetFeature(hDev, (PVOID)step.data.data(), static_cast<ULONG>(step.data.size()));
            } else if (step.type == InitStepType::SetOutputReport) {
                ULONG len = static_cast<ULONG>(step.data.size());
                const void* ptr = step.data.data();
                std::vector<uint8_t> padded;
                if (outLen > 0 && static_cast<ULONG>(outLen) > len) {
                    padded.assign(outLen, 0);
                    std::memcpy(padded.data(), step.data.data(), len);
                    ptr = padded.data();
                    len = static_cast<ULONG>(outLen);
                }
                if (!HidD_SetOutputReport(hDev, (PVOID)ptr, len)) {
                    writeOverlapped(ptr, len);
                }
                if (VendorID::IsXPPenFamily(g_spec.vid) && featLen > 0) {
                    HidD_SetFeature(hDev, (PVOID)step.data.data(), static_cast<ULONG>(step.data.size()));
                }
            } else if (step.type == InitStepType::WriteFile) {
                writeOverlapped(step.data.data(), static_cast<DWORD>(step.data.size()));
            }
        }
    };

    while (!stoken.stop_requested()) {
        if (g_removalPending.load(std::memory_order_acquire)) {
            g_removalPending.store(false, std::memory_order_relaxed);
            closeAllSlots();
            WaitForSingleObject(g_hDeviceChangeEvent, 200);
            continue;
        }

        if (numSlots == 0) {
            std::vector<std::string> devPaths = DetectAndInitTablet();
            if (devPaths.empty()) {
                WaitForSingleObject(g_hDeviceChangeEvent, 500);
                continue;
            }

            std::string conf = GetConflictingDriverProcess();
            if (!conf.empty()) {
                LogMessage("Hint: Conflicting driver found: " + conf);
            }

            int opened = 0;
            for (size_t i = 0; i < devPaths.size() && opened < MAX_ENDPOINTS; ++i) {
                HANDLE hDev = OpenTabletHandle(devPaths[i], i == 0);
                if (hDev == INVALID_HANDLE_VALUE) {
                    if (g_debugMode) {
                        DWORD err = GetLastError();
                        printf("[DEBUG] Failed to open endpoint %zu (err %lu): %s\n", i, err, devPaths[i].c_str());
                        fflush(stdout);
                    }
                    continue;
                }

                slots[opened].hDevice = hDev;
                slots[opened].path = devPaths[i];
                slots[opened].inputReportLen = g_spec.report_len;
                slots[opened].outputReportLen = 0;
                slots[opened].featureReportLen = 0;

                PHIDP_PREPARSED_DATA ppData = nullptr;
                if (HidD_GetPreparsedData(hDev, &ppData)) {
                    HIDP_CAPS caps;
                    if (HidP_GetCaps(ppData, &caps) == HIDP_STATUS_SUCCESS) {
                        slots[opened].inputReportLen = caps.InputReportByteLength;
                        slots[opened].outputReportLen = caps.OutputReportByteLength;
                        slots[opened].featureReportLen = caps.FeatureReportByteLength;
                    }
                    HidD_FreePreparsedData(ppData);
                }

                bool eventsOk = true;
                for (int b = 0; b < BUFFERS_PER_SLOT; ++b) {
                    slots[opened].io[b].ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
                    slots[opened].io[b].isPending = false;
                    if (!slots[opened].io[b].ov.hEvent) eventsOk = false;
                }

                if (!eventsOk) {
                    CloseHandle(hDev);
                    continue;
                }

                slots[opened].nextToRead = 0;
                sendTabletWakeHandshake(hDev, slots[opened].outputReportLen, slots[opened].featureReportLen);
                HidD_FlushQueue(hDev);

                if (g_debugMode) {
                    printf("[DEBUG] Successfully opened endpoint %d: inLen=%d outLen=%d featLen=%d\n",
                           opened, slots[opened].inputReportLen, slots[opened].outputReportLen, slots[opened].featureReportLen);
                    fflush(stdout);
                }
                opened++;
            }

            if (opened == 0) {
                WaitForSingleObject(g_hDeviceChangeEvent, 500);
                continue;
            }

            numSlots = opened;
            g_hDevice.store(slots[0].hDevice, std::memory_order_release);
            g_numActiveEndpoints.store(numSlots, std::memory_order_relaxed);
            rawReportsCount = 0;
            g_rawReportsReceived.store(0, std::memory_order_relaxed);
            consecutiveErrors = 0;

            TransformConfig curCfg = g_cfgStore.Load();
            if (curCfg.width_mm > 0.0 && curCfg.height_mm > 0.0) {
                SetTransform(curCfg.width_mm, curCfg.height_mm, curCfg.cx, curCfg.cy, curCfg.rotation_deg, curCfg.monitor_idx);
            } else {
                SetDefaults();
            }
            if (g_hwnd) {
                PostMessageA(g_hwnd, WM_USER + 10, 0, 0);
            }
        }

        for (int i = 0; i < numSlots; ++i) {
            if (slots[i].inputReportLen <= 0) continue;
            int bytesToRead = (std::min)(slots[i].inputReportLen, static_cast<int>(sizeof(slots[i].io[0].buf)));

            for (int b = 0; b < BUFFERS_PER_SLOT; ++b) {
                if (slots[i].io[b].isPending) continue;

                slots[i].io[b].ov.Internal = 0;
                slots[i].io[b].ov.InternalHigh = 0;
                slots[i].io[b].ov.Offset = 0;
                slots[i].io[b].ov.OffsetHigh = 0;

                BOOL readOk = ReadFile(slots[i].hDevice, slots[i].io[b].buf, bytesToRead, nullptr, &slots[i].io[b].ov);
                if (readOk) {
                    slots[i].io[b].isPending = true;
                } else {
                    DWORD err = GetLastError();
                    if (err == ERROR_IO_PENDING) {
                        slots[i].io[b].isPending = true;
                    } else if (err == ERROR_DEVICE_NOT_CONNECTED || err == ERROR_GEN_FAILURE) {
                        closeAllSlots();
                        g_reconnectCount.fetch_add(1, std::memory_order_relaxed);
                        WaitForSingleObject(g_hDeviceChangeEvent, 1000);
                        break;
                    } else {
                        g_lastReadError.store(err, std::memory_order_relaxed);
                        slots[i].io[b].isPending = false;
                        if (b == 0) {
                            if (++consecutiveErrors >= 3) {
                                closeAllSlots();
                                g_reconnectCount.fetch_add(1, std::memory_order_relaxed);
                                consecutiveErrors = 0;
                                WaitForSingleObject(g_hDeviceChangeEvent, 1000);
                                break;
                            }
                            Sleep(2);
                        }
                    }
                }
            }
            if (numSlots == 0) break;
        }

        if (numSlots == 0) continue;

        int readySlot = -1;
        int readyBuf = -1;

        for (int i = 0; i < numSlots; ++i) {
            int exp = slots[i].nextToRead;
            if (slots[i].io[exp].isPending) {
                if (HasOverlappedIoCompleted(&slots[i].io[exp].ov)) {
                    readySlot = i;
                    readyBuf = exp;
                    break;
                }
            }
            int other = 1 - exp;
            if (slots[i].io[other].isPending) {
                if (HasOverlappedIoCompleted(&slots[i].io[other].ov)) {
                    readySlot = i;
                    readyBuf = other;
                    break;
                }
            }
        }

        if (readySlot == -1) {
            HANDLE waitHandles[MAX_ENDPOINTS + 1];
            int waitMap[MAX_ENDPOINTS];
            int waitCount = 0;

            for (int i = 0; i < numSlots; ++i) {
                int exp = slots[i].nextToRead;
                if (slots[i].io[exp].isPending) {
                    waitMap[waitCount] = i;
                    waitHandles[waitCount++] = slots[i].io[exp].ov.hEvent;
                }
            }
            waitHandles[waitCount] = g_hDeviceChangeEvent;

            if (waitCount > 0) {
                DWORD waitRes = WaitForMultipleObjects(waitCount + 1, waitHandles, FALSE, 200);
                if (waitRes >= WAIT_OBJECT_0 && waitRes < (WAIT_OBJECT_0 + waitCount)) {
                    int idx = waitRes - WAIT_OBJECT_0;
                    readySlot = waitMap[idx];
                    readyBuf = slots[readySlot].nextToRead;
                } else if (waitRes == (WAIT_OBJECT_0 + waitCount)) {
                    closeAllSlots();
                    continue;
                }
            }
        }

        if (readySlot == -1 || stoken.stop_requested()) {
            continue;
        }

        int slotIdx = readySlot;
        int bufIdx = readyBuf;
        DWORD transferred = 0;

        if (slots[slotIdx].io[bufIdx].ov.Internal == 0 && slots[slotIdx].io[bufIdx].ov.InternalHigh > 0) {
            transferred = static_cast<DWORD>(slots[slotIdx].io[bufIdx].ov.InternalHigh);
        } else {
            if (!GetOverlappedResult(slots[slotIdx].hDevice, &slots[slotIdx].io[bufIdx].ov, &transferred, FALSE)) {
                DWORD err = GetLastError();
                if (err != ERROR_IO_INCOMPLETE) {
                    g_lastReadError.store(err, std::memory_order_relaxed);
                    if (++consecutiveErrors >= 3) {
                        closeAllSlots();
                        g_reconnectCount.fetch_add(1, std::memory_order_relaxed);
                        consecutiveErrors = 0;
                        WaitForSingleObject(g_hDeviceChangeEvent, 1000);
                    } else {
                        slots[slotIdx].io[bufIdx].isPending = false;
                    }
                }
                continue;
            }
        }

        LARGE_INTEGER qpc;
        QueryPerformanceCounter(&qpc);

        alignas(64) BYTE packetCopy[48];
        uint32_t reportBytes = 0;
        bool hasPacket = false;
        if (transferred > 0) {
            reportBytes = (std::min)(transferred, static_cast<DWORD>(sizeof(packetCopy)));
            std::memcpy(packetCopy, slots[slotIdx].io[bufIdx].buf, reportBytes);
            hasPacket = true;
        }

        // Pipelined rearm of ReadFile before performing heavy operations:
        // Rearm the USB transfer immediately so the hardware host controller and Windows kernel
        // begin fetching the next packet in parallel while user-mode performs ring enqueuing,
        // event signaling (SetEvent syscall), atomics, and diagnostic logging.
        int bytesToRead = (std::min)(slots[slotIdx].inputReportLen, static_cast<int>(sizeof(slots[slotIdx].io[bufIdx].buf)));
        slots[slotIdx].io[bufIdx].ov.Internal = 0;
        slots[slotIdx].io[bufIdx].ov.InternalHigh = 0;
        slots[slotIdx].io[bufIdx].ov.Offset = 0;
        slots[slotIdx].io[bufIdx].ov.OffsetHigh = 0;

        BOOL rearmOk = ReadFile(slots[slotIdx].hDevice, slots[slotIdx].io[bufIdx].buf, bytesToRead, nullptr, &slots[slotIdx].io[bufIdx].ov);
        if (rearmOk || GetLastError() == ERROR_IO_PENDING) {
            slots[slotIdx].io[bufIdx].isPending = true;
        } else {
            DWORD err = GetLastError();
            if (err == ERROR_DEVICE_NOT_CONNECTED || err == ERROR_GEN_FAILURE) {
                closeAllSlots();
                g_reconnectCount.fetch_add(1, std::memory_order_relaxed);
                continue;
            } else {
                g_lastReadError.store(err, std::memory_order_relaxed);
                slots[slotIdx].io[bufIdx].isPending = false;
            }
        }

        slots[slotIdx].nextToRead = 1 - bufIdx;
        consecutiveErrors = 0;

        if (hasPacket) {
            g_packetRing.TryPush(packetCopy, reportBytes, static_cast<uint32_t>(slotIdx), static_cast<uint64_t>(qpc.QuadPart));
            std::atomic_thread_fence(std::memory_order_seq_cst);
            if (g_dispatcherWaiting.load(std::memory_order_relaxed) && g_hPacketReady) {
                SetEvent(g_hPacketReady);
            }

            rawReportsCount++;
            g_rawReportsReceived.store(rawReportsCount, std::memory_order_relaxed);
            if (g_debugMode && slotIdx >= 0 && slotIdx < 4) {
                g_epCount[slotIdx].fetch_add(1, std::memory_order_relaxed);
                if (reportBytes >= 4) {
                    uint32_t b4 = 0;
                    std::memcpy(&b4, packetCopy, sizeof(b4));
                    g_lastReportBytes.store(b4, std::memory_order_relaxed);
                }
            }
        }
    }

    closeAllSlots();

    if (hMmcss) {
        AvRevertMmThreadCharacteristics(hMmcss);
    }
}

void InputDispatchThread(std::stop_token stoken) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    if (g_affinityConfig.dispatcher != 0) {
        SetThreadAffinityMask(GetCurrentThread(), g_affinityConfig.dispatcher);
        SetThreadIdealProcessor(GetCurrentThread(), g_affinityConfig.dispatcherCore);
    }

    DisablePowerThrottling();
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);

    HANDLE hMmcss = nullptr;
    if (GetPriorityClass(GetCurrentProcess()) != REALTIME_PRIORITY_CLASS) {
        DWORD taskIndex = 0;
        hMmcss = AvSetMmThreadCharacteristicsA("Pro Audio", &taskIndex);
        if (hMmcss) {
            AvSetMmThreadPriority(hMmcss, AVRT_PRIORITY_CRITICAL);
        }
    }

    uint64_t lastPackedDxy = 0xFFFFFFFFFFFFFFFFULL;
    int32_t lastRawX = -1;
    int32_t lastRawY = -1;
    bool lastPressed = false;
    bool lastRightPressed = false;
    int inProximityFrames = 0;
    uint32_t localSeq = 0xFFFFFFFF;
    TransformConfig localCfg{};
    PrevPacketData prevPacket{};
    KinematicExtrapolator extrapolator;

    uint64_t localLastReportQpc = 0;
    uint64_t localMeasuredIntervalQpc = 0;
    bool lastInRange = false;
    INPUT mouseInput{};
    mouseInput.type = INPUT_MOUSE;

    const __m128d vZero = _mm_setzero_pd();
    const __m128d vOne = _mm_set1_pd(1.0);
    const __m128d vMaxVal = _mm_set1_pd(65535.0);

    __m128d vknx = _mm_setzero_pd();
    __m128d vkny = _mm_setzero_pd();
    __m128d vknc = _mm_setzero_pd();
    __m128d vvscale = _mm_set1_pd(65535.0);
    __m128d vvshift = _mm_set1_pd(0.5);
    __m128d vmx = _mm_setzero_pd();
    __m128d vmy = _mm_setzero_pd();
    __m128d vmc = _mm_setzero_pd();

    int rangeCounter = 0;
    bool activeTip = false;
    uint64_t lastInRangeQpc = 0;

    FILE* dumpReportsFp = nullptr;
    if (!g_dumpReportsFile.empty()) {
        dumpReportsFp = fopen(g_dumpReportsFile.c_str(), "ab");
    }
    uint64_t last_reject_qpc = 0;
    uint64_t last_trace_qpc = 0;
    uint64_t frame_count = 0;
    int32_t min_x = 9999999, max_x = -9999999;
    int32_t min_y = 9999999, max_y = -9999999;
    DWORD lastErrTime = 0;

    auto sendMouseUp = [](DWORD flag) noexcept {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = flag;
        DirectSendInput(1, &in, sizeof(INPUT));
    };

    auto resetState = [&]() noexcept {
        if (lastPressed) {
            sendMouseUp(MOUSEEVENTF_LEFTUP);
            lastPressed = false;
        }
        if (lastRightPressed) {
            sendMouseUp(MOUSEEVENTF_RIGHTUP);
            lastRightPressed = false;
        }
        inProximityFrames = 0;
        lastInRange = false;
        activeTip = false;
        rangeCounter = 0;
        lastPackedDxy = 0xFFFFFFFFFFFFFFFFULL;
        lastRawX = -1;
        lastRawY = -1;
        localLastReportQpc = 0;
        extrapolator.Reset();
    };

    auto checkWatchdog = [&](uint64_t nowQpc) noexcept {
        if (localLastReportQpc == 0) return;

        uint64_t interval = localMeasuredIntervalQpc;
        uint64_t minWaitQpc = (static_cast<uint64_t>(g_qpcFreq.QuadPart) * 15) / 1000;
        uint64_t watchdogThreshold = (std::max)(minWaitQpc, interval * 4);

        if ((nowQpc - localLastReportQpc) > watchdogThreshold) {
            if (lastPressed && g_debugMode) {
                ConsoleLock();
                printf("[EVENT] Synthetic click injected: UP (watchdog timeout)\n");
                fflush(stdout);
                ConsoleUnlock();
            }
            if (inProximityFrames > 0 && g_debugMode) {
                ConsoleLock();
                printf("[EVENT] LEFT PROXIMITY (watchdog timeout)\n");
                fflush(stdout);
                ConsoleUnlock();
            }
            resetState();
        }
    };

    while (!stoken.stop_requested()) {
        RawPacket* pPkt = g_packetRing.Peek();

        if (!pPkt) {
            if (localLastReportQpc > 0) {
                const int spinPct = localCfg.spin_percent;
                if (spinPct <= 0) {
                    // Ultra-low CPU mode (<0.2% CPU): brief L1 burst check (64 pauses ~ 1.5 us), then kernel wait
                    for (uint32_t spin = 0; spin < 64u && !stoken.stop_requested(); ++spin) {
                        _mm_pause();
                        pPkt = g_packetRing.Peek();
                        if (pPkt) break;
                    }
                } else if (g_qpcToUs > 0.0) {
                    const uint64_t baseIntvUs = (localMeasuredIntervalQpc > 0)
                        ? (std::min)(static_cast<uint64_t>(localMeasuredIntervalQpc * g_qpcToUs) + 400ULL, 8500ULL)
                        : 4000ULL;
                    const uint64_t maxSpinUs = (baseIntvUs * static_cast<uint64_t>(spinPct)) / 100ULL;
                    LARGE_INTEGER qSpinStart;
                    QueryPerformanceCounter(&qSpinStart);
                    const uint64_t spinStartQpc = static_cast<uint64_t>(qSpinStart.QuadPart);
                    for (uint32_t spin = 0; !stoken.stop_requested(); ++spin) {
                        _mm_pause();
                        _mm_pause();
                        _mm_pause();
                        _mm_pause();
                        pPkt = g_packetRing.Peek();
                        if (pPkt) break;
                        if ((spin & 31u) == 31u) {
                            LARGE_INTEGER qCur;
                            QueryPerformanceCounter(&qCur);
                            if (static_cast<uint64_t>((qCur.QuadPart - spinStartQpc) * g_qpcToUs) >= maxSpinUs) {
                                break;
                            }
                        }
                    }
                }
            }

            if (!pPkt && !stoken.stop_requested()) {
                LARGE_INTEGER qNow;
                QueryPerformanceCounter(&qNow);
                checkWatchdog(static_cast<uint64_t>(qNow.QuadPart));
                g_dispatcherWaiting.store(true, std::memory_order_seq_cst);
                pPkt = g_packetRing.Peek();
                if (pPkt) {
                    g_dispatcherWaiting.store(false, std::memory_order_relaxed);
                } else {
                    DWORD waitTimeout = (localLastReportQpc > 0) ? 5 : 500;
                    WaitForSingleObject(g_hPacketReady, waitTimeout);
                    g_dispatcherWaiting.store(false, std::memory_order_relaxed);
                    pPkt = g_packetRing.Peek();
                }
            }
        }

        if (!pPkt || stoken.stop_requested()) {
            continue;
        }

        if (pPkt->endpointIdx == static_cast<uint32_t>(-1)) {
            resetState();
            g_packetRing.Pop();
            continue;
        }

        const BYTE* data = pPkt->data;
        int len = static_cast<int>(pPkt->len);
        int endpointIdx = static_cast<int>(pPkt->endpointIdx);
        uint64_t curNowQpc = pPkt->qpc;

        PenSample out_sample{};
        DecodeReason reason = DecodeSample(data, len, g_spec, out_sample);

        if (dumpReportsFp) {
            fwrite(data, 1, len, dumpReportsFp);
            fflush(dumpReportsFp);
        }

        g_packetRing.Pop();

        if (g_debugMode) {
            bool is_reject = (reason != DecodeReason::Ok && reason != DecodeReason::ZeroFrame && reason != DecodeReason::LiftSignature);
            if (is_reject) {
                if ((curNowQpc - last_reject_qpc) >= static_cast<uint64_t>(g_qpcFreq.QuadPart / 10)) {
                    last_reject_qpc = curNowQpc;
                    char hexBuf[48] = "";
                    int hexLen = (std::min)(len, 12);
                    int pos = 0;
                    for (int i = 0; i < hexLen; ++i) pos += snprintf(hexBuf + pos, sizeof(hexBuf) - pos, "%02X ", data[i]);
                    ConsoleLock();
                    printf("[REJECT] EP%d len=%d [%s] reason=%s\n", endpointIdx, len, hexBuf, DecodeReasonToString(reason));
                    fflush(stdout);
                    ConsoleUnlock();
                }
            }
        }

        if (reason == DecodeReason::ZeroFrame) {
            extrapolator.Reset();
            if (activeTip || lastPressed || lastRightPressed) {
                out_sample.flags = 0;
                out_sample.raw_x = prevPacket.x;
                out_sample.raw_y = prevPacket.y;
                reason = DecodeReason::Ok;
            } else {
                inProximityFrames = 0;
                rangeCounter = 0;
                if (g_debugMode) [[unlikely]] {
                    g_livePen.current.in_range = 0;
                }
            }
        } else if (reason == DecodeReason::Ok) {
            rangeCounter++;
            lastInRangeQpc = curNowQpc;
        } else if (reason == DecodeReason::LiftSignature) {
            extrapolator.Reset();
            if (activeTip || lastPressed || lastRightPressed) {
                out_sample.flags = 0;
                out_sample.raw_x = prevPacket.x;
                out_sample.raw_y = prevPacket.y;
                reason = DecodeReason::Ok;
            } else {
                inProximityFrames = 0;
                rangeCounter = 0;
                extrapolator.Reset();
                if (g_debugMode) [[unlikely]] {
                    g_livePen.current.in_range = 0;
                }
            }
        }

        if (reason != DecodeReason::Ok) {
            if (g_debugMode && endpointIdx == 0) [[unlikely]] {
                if (reason != DecodeReason::LiftSignature && reason != DecodeReason::ZeroFrame) {
                    g_wacomStats.rejected.fetch_add(1, std::memory_order_relaxed);
                }
            }
            continue;
        }

        int32_t rx = out_sample.raw_x, ry = out_sample.raw_y;
        bool pressed = (out_sample.flags & 1) != 0;
        activeTip = pressed;

        if (g_spec.max_x > 0 && g_spec.max_y > 0 && prevPacket.qpc > 0) {
            if (g_debugMode) [[unlikely]] {
                if ((curNowQpc - prevPacket.qpc) * 1000 / g_qpcFreq.QuadPart < 20) {
                    double dx = (double)(rx - prevPacket.x) / g_spec.max_x;
                    double dy = (double)(ry - prevPacket.y) / g_spec.max_y;
                    if (dx*dx + dy*dy > 0.0625) {
                        g_wacomStats.glitches.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        }
        prevPacket.qpc = curNowQpc;
        prevPacket.x = rx;
        prevPacket.y = ry;
        prevPacket.len = len;

        if (localLastReportQpc > 0 && curNowQpc > localLastReportQpc) {
            uint64_t intv = curNowQpc - localLastReportQpc;
            if (intv >= (static_cast<uint64_t>(g_qpcFreq.QuadPart) / 5000) &&
                intv < static_cast<uint64_t>(g_qpcFreq.QuadPart / 10)) {
                if (localMeasuredIntervalQpc == 0) {
                    localMeasuredIntervalQpc = intv;
                } else {
                    localMeasuredIntervalQpc = (localMeasuredIntervalQpc * 7 + intv) / 8;
                }
                if (g_debugMode && g_qpcToUs > 0.0) [[unlikely]] {
                    uint64_t intvUs = static_cast<uint64_t>(intv * g_qpcToUs);
                    if (intvUs >= 200 && intvUs <= 50000) {
                        g_jitterStats.RecordInterval(intvUs);
                    }
                }
            }
        }
        localLastReportQpc = curNowQpc;
        if (g_debugMode) {
            g_wacomStats.accepted.fetch_add(1, std::memory_order_relaxed);
            g_reportsReceived.fetch_add(1, std::memory_order_relaxed);
        }

        out_sample.qpc = curNowQpc;
        out_sample.endpoint = static_cast<uint8_t>(endpointIdx);

        if (inProximityFrames < 3) {
            inProximityFrames++;
        }

        const bool penClick = g_penClick.load(std::memory_order_relaxed);
        const bool barrelClick = g_barrelClick.load(std::memory_order_relaxed);

        const bool rawTip = ((out_sample.flags & 1) != 0);
        const bool rawHwTip = ((out_sample.flags & 0x09) != 0);
        const uint16_t pressure = out_sample.pressure;
        const uint16_t thresh = g_spec.tip_pressure_threshold;

        bool curPressed = false;
        if (inProximityFrames > 0 && penClick) {
            if (thresh > 0) {
                const uint16_t releaseThresh = (thresh > 4) ? static_cast<uint16_t>(thresh * 3 / 4) : static_cast<uint16_t>(thresh - 1);
                if (lastPressed) {
                    curPressed = (pressure >= releaseThresh) && rawHwTip;
                } else {
                    curPressed = (pressure >= thresh) && rawTip;
                }
            } else {
                curPressed = rawHwTip;
            }
        }

        const bool leftBtnEdge = (curPressed != lastPressed);
        if (leftBtnEdge) {
            lastPressed = curPressed;
        }

        const bool curRightPressed = (inProximityFrames > 0) && ((out_sample.flags & 4) != 0) && barrelClick;
        const bool rightBtnEdge = (curRightPressed != lastRightPressed);
        if (rightBtnEdge) {
            lastRightPressed = curRightPressed;
        }
        const bool curInRange = (inProximityFrames > 0);
        if (curInRange != lastInRange) {
            lastInRange = curInRange;
        }

        const uint32_t prevSeq = localSeq;
        const uint32_t curSeq = g_cfgStore.GetSequence();
        if (curSeq != localSeq) [[unlikely]] {
            localCfg = g_cfgStore.Load(&localSeq);
            vknx = _mm_load_pd(localCfg.knx);
            vkny = _mm_load_pd(localCfg.kny);
            vknc = _mm_load_pd(localCfg.knc);
            vvscale = _mm_load_pd(localCfg.vscale);
            vvshift = _mm_load_pd(localCfg.vshift);
            vmx = _mm_mul_pd(vknx, vvscale);
            vmy = _mm_mul_pd(vkny, vvscale);
            vmc = FmaddPd(vknc, vvscale, vvshift);
        }

        if (rx == lastRawX && ry == lastRawY && !leftBtnEdge && !rightBtnEdge &&
            localSeq == prevSeq && !extrapolator.HasActiveLead()) [[unlikely]] {
            if (g_debugMode) g_reportsDeduped.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        // Reflex / Ultra-Low Latency: only skip if next queued packet is a valid normal endpoint report
        if (!leftBtnEdge && !rightBtnEdge) {
            const RawPacket* nextPkt = g_packetRing.Peek();
            if (nextPkt && nextPkt->len >= 4 && nextPkt->endpointIdx != static_cast<uint32_t>(-1)) {
                if (g_debugMode) g_reportsDeduped.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
        }
        lastRawX = rx;
        lastRawY = ry;

        const __m128d vfx = _mm_set1_pd(static_cast<double>(out_sample.raw_x));
        const __m128d vfy = _mm_set1_pd(static_cast<double>(out_sample.raw_y));
        __m128d vn = vZero;
        bool isOutside = false;
        __m128d vout;
        if (localCfg.clip_area || g_debugMode || g_calibrate) [[unlikely]] {
            vn = FmaddPd(vfx, vknx, FmaddPd(vfy, vkny, vknc));
            const __m128d cmpLo = _mm_cmplt_pd(vn, vZero);
            const __m128d cmpHi = _mm_cmpgt_pd(vn, vOne);
            isOutside = (_mm_movemask_pd(_mm_or_pd(cmpLo, cmpHi)) != 0);
            vout = FmaddPd(vn, vvscale, vvshift);
        } else {
            vout = FmaddPd(vfx, vmx, FmaddPd(vfy, vmy, vmc));
        }

        // Adaptive Kinematic Extrapolation (loaded thread-safely via TransformConfig):
        ExtrapolationConfig extraCfg;
        extraCfg.enabled = localCfg.extrapolation;
        extraCfg.tau_ms = localCfg.extrapolation_tau;

        const bool isClipped = (localCfg.clip_area && isOutside);
        if (isClipped) [[unlikely]] {
            extrapolator.Reset();
        } else if (extraCfg.enabled) {
            alignas(16) double rawVout[2];
            _mm_store_pd(rawVout, vout);
            double extrapX = rawVout[0];
            double extrapY = rawVout[1];
            extrapolator.Update(rawVout[0], rawVout[1], curNowQpc, static_cast<uint64_t>(g_qpcFreq.QuadPart), extraCfg, extrapX, extrapY);
            vout = _mm_set_pd(extrapY, extrapX);
        } else {
            extrapolator.Reset();
        }

        vout = _mm_min_pd(_mm_max_pd(vout, vZero), vMaxVal);

        const __m128i vi = _mm_cvttpd_epi32(vout);
        const uint64_t packedDxy = static_cast<uint64_t>(_mm_cvtsi128_si64(vi));

        if (g_debugMode) [[unlikely]] {
            alignas(16) double norm_uv[2];
            _mm_store_pd(norm_uv, vn);
            int32_t curPx = static_cast<int32_t>(packedDxy & 0xFFFFFFFF);
            int32_t curPy = static_cast<int32_t>(packedDxy >> 32);

            CompactPenSample s{};
            s.raw_x = out_sample.raw_x;
            s.raw_y = out_sample.raw_y;
            s.px = curPx;
            s.py = curPy;
            s.un = norm_uv[0];
            s.vn = norm_uv[1];
            s.pressure = static_cast<uint16_t>(out_sample.pressure);
            s.tip = (out_sample.flags & 1) ? 1 : 0;
            s.barrel = (out_sample.flags & 4) ? 1 : 0;
            s.in_range = 1;

            g_livePen.current = s;
            uint32_t idx = g_livePen.write_idx.load(std::memory_order_relaxed);
            g_livePen.history[idx % LivePenTelemetry::TRAJ_CAP] = s;
            g_livePen.write_idx.store(idx + 1, std::memory_order_relaxed);
        }

        if (localCfg.clip_area && isOutside) [[unlikely]] {
            if (lastPressed) {
                sendMouseUp(MOUSEEVENTF_LEFTUP);
                lastPressed = false;
            }
            if (lastRightPressed) {
                sendMouseUp(MOUSEEVENTF_RIGHTUP);
                lastRightPressed = false;
            }
            lastPackedDxy = 0xFFFFFFFFFFFFFFFFULL;
            extrapolator.Reset();
            continue;
        }

        if (g_calibrate) {
            frame_count++;
            int32_t px = (int32_t)(packedDxy & 0xFFFFFFFF);
            int32_t py = (int32_t)(packedDxy >> 32);
            if (px < min_x) min_x = px;
            if (px > max_x) max_x = px;
            if (py < min_y) min_y = py;
            if (py > max_y) max_y = py;

            if (frame_count % 64 == 0) {
                alignas(16) double un_vn[2];
                _mm_store_pd(un_vn, vn);
                double inv_sx = g_spec.phys_w / static_cast<double>(g_spec.max_x);
                double inv_sy = g_spec.phys_h / static_cast<double>(g_spec.max_y);
                printf("[TRACE] frame=%llu raw=(%d,%d) -> mm=(%.2f,%.2f) -> un/vn=(%.5f,%.5f) -> px=(%d,%d) | min=(%d,%d) max=(%d,%d)\n",
                    (unsigned long long)frame_count, out_sample.raw_x, out_sample.raw_y,
                    out_sample.raw_x * inv_sx, out_sample.raw_y * inv_sy,
                    un_vn[0], un_vn[1], px, py,
                    min_x, min_y, max_x, max_y);
            }
        }

        if (packedDxy == lastPackedDxy && !leftBtnEdge && !rightBtnEdge) [[unlikely]] {
            if (g_debugMode) g_reportsDeduped.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        lastPackedDxy = packedDxy;

        std::memcpy(&mouseInput.mi.dx, &packedDxy, sizeof(packedDxy));
#ifndef MOUSEEVENTF_MOVE_NOCOALESCE
#define MOUSEEVENTF_MOVE_NOCOALESCE 0x2000
#endif
        DWORD dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | MOUSEEVENTF_MOVE_NOCOALESCE;
        if (leftBtnEdge) {
            dwFlags |= curPressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        }
        if (rightBtnEdge) {
            dwFlags |= curRightPressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
        }
        mouseInput.mi.dwFlags = dwFlags;

        if (DirectSendInput(1, &mouseInput, sizeof(INPUT)) == 0) [[unlikely]] {
            DWORD nowTick = GetTickCount();
            if (nowTick - lastErrTime > 1000) {
                LogMessage("ERROR: SendInput failed with error " + std::to_string(GetLastError()));
                lastErrTime = nowTick;
            }
        } else if (g_debugMode) [[unlikely]] {
            LARGE_INTEGER postInput;
            QueryPerformanceCounter(&postInput);
            uint64_t endQpc = static_cast<uint64_t>(postInput.QuadPart);
            uint64_t startQpc = out_sample.qpc;
            if (endQpc >= startQpc && g_qpcToUs > 0.0) {
                uint64_t latUs = static_cast<uint64_t>((endQpc - startQpc) * g_qpcToUs);
                g_latencyStats.Record(latUs);
            }
        }
    }

    resetState();

    if (dumpReportsFp) {
        fclose(dumpReportsFp);
        dumpReportsFp = nullptr;
    }

    if (hMmcss) {
        AvRevertMmThreadCharacteristics(hMmcss);
    }
}

void StartDriverPipeline() {
    if (!g_hPacketReady) {
        g_hPacketReady = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    }
    if (!g_hDeviceChangeEvent) {
        g_hDeviceChangeEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    }
    QueryPerformanceFrequency(&g_qpcFreq);
    g_qpcToUs = (g_qpcFreq.QuadPart > 0) ? (1000000.0 / static_cast<double>(g_qpcFreq.QuadPart)) : 0.0;

    g_readerThread = std::jthread(ReaderThread);
    g_dispatchThread = std::jthread(InputDispatchThread);
}

void StopDriverPipeline() {
    g_readerThread.request_stop();
    g_dispatchThread.request_stop();

    if (g_hPacketReady) {
        SetEvent(g_hPacketReady);
    }
    HANDLE hDev = g_hDevice.load(std::memory_order_acquire);
    if (hDev != INVALID_HANDLE_VALUE) {
        CancelIoEx(hDev, nullptr);
    }
    if (g_hDeviceChangeEvent) {
        SetEvent(g_hDeviceChangeEvent);
    }
    if (g_readerThread.joinable()) g_readerThread.join();
    if (g_dispatchThread.joinable()) g_dispatchThread.join();

    if (g_hDeviceChangeEvent) { CloseHandle(g_hDeviceChangeEvent); g_hDeviceChangeEvent = NULL; }
    if (g_hPacketReady) { CloseHandle(g_hPacketReady); g_hPacketReady = NULL; }
}
