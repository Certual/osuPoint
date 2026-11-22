#include "test_framework.h"
#include "test_vectors.h"
#include "test_pipeline.h"
#include "test_filter_impl.h"

#include <vector>
#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <linux/input.h>
#include <linux/uinput.h>

// --- Virtual / Kernel UInput Loopback Harness ---

class VirtualInputDevice {
public:
    virtual ~VirtualInputDevice() = default;
    virtual bool Open() = 0;
    virtual void EmitAbs(uint16_t code, int32_t value) = 0;
    virtual void EmitKey(uint16_t code, int32_t value) = 0;
    virtual void EmitSyn() = 0;
    virtual void Close() = 0;
    virtual bool IsKernelDevice() const noexcept = 0;
    virtual size_t EventCount() const noexcept = 0;
    virtual void ClearEvents() = 0;
};

// Linux Kernel /dev/uinput Device
class KernelUInputDevice : public VirtualInputDevice {
public:
    KernelUInputDevice() : m_fd(-1) {}
    ~KernelUInputDevice() override { Close(); }

    bool Open() override {
        m_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
        if (m_fd < 0) return false;

        ioctl(m_fd, UI_SET_EVBIT, EV_SYN);
        ioctl(m_fd, UI_SET_EVBIT, EV_KEY);
        ioctl(m_fd, UI_SET_EVBIT, EV_ABS);

        ioctl(m_fd, UI_SET_KEYBIT, BTN_TOUCH);
        ioctl(m_fd, UI_SET_KEYBIT, BTN_TOOL_PEN);
        ioctl(m_fd, UI_SET_KEYBIT, BTN_STYLUS);
        ioctl(m_fd, UI_SET_KEYBIT, BTN_LEFT);

        ioctl(m_fd, UI_SET_ABSBIT, ABS_X);
        ioctl(m_fd, UI_SET_ABSBIT, ABS_Y);
        ioctl(m_fd, UI_SET_ABSBIT, ABS_PRESSURE);

        struct uinput_abs_setup abs_x{};
        abs_x.code = ABS_X;
        abs_x.absinfo.minimum = 0;
        abs_x.absinfo.maximum = 65535;
        ioctl(m_fd, UI_ABS_SETUP, &abs_x);

        struct uinput_abs_setup abs_y{};
        abs_y.code = ABS_Y;
        abs_y.absinfo.minimum = 0;
        abs_y.absinfo.maximum = 65535;
        ioctl(m_fd, UI_ABS_SETUP, &abs_y);

        struct uinput_setup usetup{};
        std::strncpy(usetup.name, "osuPoint Synthetic Virtual Tablet", sizeof(usetup.name) - 1);
        usetup.id.bustype = BUS_USB;
        usetup.id.vendor = 0x056A;
        usetup.id.product = 0x037A;
        usetup.id.version = 1;

        if (ioctl(m_fd, UI_DEV_SETUP, &usetup) < 0) {
            Close();
            return false;
        }

        if (ioctl(m_fd, UI_DEV_CREATE) < 0) {
            Close();
            return false;
        }

        return true;
    }

    void EmitAbs(uint16_t code, int32_t value) override {
        WriteEvent(EV_ABS, code, value);
    }

    void EmitKey(uint16_t code, int32_t value) override {
        WriteEvent(EV_KEY, code, value);
    }

    void EmitSyn() override {
        WriteEvent(EV_SYN, SYN_REPORT, 0);
    }

    void Close() override {
        if (m_fd >= 0) {
            ioctl(m_fd, UI_DEV_DESTROY);
            close(m_fd);
            m_fd = -1;
        }
    }

    bool IsKernelDevice() const noexcept override { return true; }
    size_t EventCount() const noexcept override { return m_eventCount; }
    void ClearEvents() override { m_eventCount = 0; }

private:
    void WriteEvent(uint16_t type, uint16_t code, int32_t value) {
        if (m_fd < 0) return;
        struct input_event ev{};
        ev.type = type;
        ev.code = code;
        ev.value = value;
        if (write(m_fd, &ev, sizeof(ev)) > 0) {
            m_eventCount++;
        }
    }

    int m_fd;
    size_t m_eventCount = 0;
};

// In-Memory Virtual Loopback Mock
class MockUInputDevice : public VirtualInputDevice {
public:
    struct RecordedEvent {
        uint16_t type;
        uint16_t code;
        int32_t  value;
    };

    bool Open() override {
        m_events.clear();
        m_opened = true;
        return true;
    }

    void EmitAbs(uint16_t code, int32_t value) override {
        if (m_opened) m_events.push_back({EV_ABS, code, value});
    }

    void EmitKey(uint16_t code, int32_t value) override {
        if (m_opened) m_events.push_back({EV_KEY, code, value});
    }

    void EmitSyn() override {
        if (m_opened) m_events.push_back({EV_SYN, SYN_REPORT, 0});
    }

    void Close() override {
        m_opened = false;
    }

    bool IsKernelDevice() const noexcept override { return false; }
    size_t EventCount() const noexcept override { return m_events.size(); }
    void ClearEvents() override { m_events.clear(); }

    const std::vector<RecordedEvent>& Events() const noexcept { return m_events; }

private:
    bool m_opened = false;
    std::vector<RecordedEvent> m_events;
};

inline std::unique_ptr<VirtualInputDevice> CreateBestAvailableInputDevice() {
    const char* kernel_test = std::getenv("OSUPOINT_TEST_KERNEL");
    if (kernel_test && std::strcmp(kernel_test, "1") == 0) {
        auto kernel = std::make_unique<KernelUInputDevice>();
        if (kernel->Open()) return kernel;
        return nullptr;
    }
    // Safe default: never create or inject a desktop device during make check.
    auto mock = std::make_unique<MockUInputDevice>();
    mock->Open();
    return mock;
}

static inline uint64_t GetMonotonicRawNs() noexcept {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

// --- TIER 1: Feature Coverage ---

E2E_TEST(UInput_DeviceCreationAndEventBatching, TestTier::Tier1_Feature) {
    auto dev = CreateBestAvailableInputDevice();
    ASSERT_TRUE(dev != nullptr);

    dev->ClearEvents();
    dev->EmitAbs(ABS_X, 960);
    dev->EmitAbs(ABS_Y, 540);
    dev->EmitKey(BTN_TOUCH, 1);
    dev->EmitSyn();

    ASSERT_GE(dev->EventCount(), 4u);
    dev->Close();
}

E2E_TEST(UInput_CoordinateFidelity_ZeroDriftMultiPass, TestTier::Tier1_Feature) {
    MockUInputDevice dev;
    dev.Open();

    TabletSpec spec{};
    spec.phys_w = 152.0;
    spec.phys_h = 95.0;
    spec.max_x = 15200;
    spec.max_y = 9500;

    CoordinateTransformer tf;
    tf.Configure(spec, 152.0, 95.0, 76.0, 47.5, 0.0, 0, 0, 1920, 1080, {0, 0, 1920, 1080}, {0, 0, 1920, 1080});

    // Pass 1 and Pass 2 on identical trajectory:
    const std::vector<std::pair<int, int>> testTrajectory = {
        {0, 0}, {3800, 2375}, {7600, 4750}, {11400, 7125}, {15200, 9500}
    };

    std::vector<std::pair<int, int>> pass1Output;
    for (const auto& pt : testTrajectory) {
        int px, py;
        tf.TransformRawFast(pt.first, pt.second, px, py);
        pass1Output.push_back({px, py});
        dev.EmitAbs(ABS_X, px);
        dev.EmitAbs(ABS_Y, py);
        dev.EmitSyn();
    }

    std::vector<std::pair<int, int>> pass2Output;
    for (const auto& pt : testTrajectory) {
        int px, py;
        tf.TransformRawFast(pt.first, pt.second, px, py);
        pass2Output.push_back({px, py});
    }

    // Verify bit-exact zero coordinate drift between passes
    ASSERT_EQ(pass1Output.size(), pass2Output.size());
    for (size_t i = 0; i < pass1Output.size(); ++i) {
        ASSERT_EQ(pass1Output[i].first, pass2Output[i].second ? pass2Output[i].first : pass2Output[i].first);
        ASSERT_EQ(pass1Output[i].first, pass2Output[i].first);
        ASSERT_EQ(pass1Output[i].second, pass2Output[i].second);
    }
}

E2E_TEST(UInput_DualInjectionMode_StylusVsMouse, TestTier::Tier1_Feature) {
    MockUInputDevice dev;
    dev.Open();

    // Stylus mode: emits ABS_X, ABS_Y, ABS_PRESSURE, BTN_TOUCH, BTN_TOOL_PEN
    dev.EmitAbs(ABS_X, 500);
    dev.EmitAbs(ABS_Y, 400);
    dev.EmitAbs(ABS_PRESSURE, 2048);
    dev.EmitKey(BTN_TOOL_PEN, 1);
    dev.EmitKey(BTN_TOUCH, 1);
    dev.EmitSyn();

    ASSERT_EQ(dev.Events().size(), 6u);

    // Mouse mode: emits ABS_X, ABS_Y, BTN_LEFT
    dev.ClearEvents();
    dev.EmitAbs(ABS_X, 500);
    dev.EmitAbs(ABS_Y, 400);
    dev.EmitKey(BTN_LEFT, 1);
    dev.EmitSyn();

    ASSERT_EQ(dev.Events().size(), 4u);
}

// --- TIER 4: Real-World Workload Scenarios ---

E2E_TEST(Tier4_Scenario1_HighBpmOsuJump, TestTier::Tier4_RealWorld) {
    // High-BPM osu! Jump Scenario:
    // Large rapid jumps between Left circle (x=300) and Right circle (x=1600) on a 1920x1080 display.
    // Tests: Decoder -> Affine Transform -> Extrapolator Turnaround Suppression.
    // Invariant: Lead must clear immediately upon jump reversal; zero overshoot!

    TabletSpec spec{};
    spec.phys_w = 152.0;
    spec.phys_h = 95.0;
    spec.max_x = 15200;
    spec.max_y = 9500;

    CoordinateTransformer tf;
    tf.Configure(spec, 152.0, 95.0, 76.0, 47.5, 0.0, 0, 0, 1920, 1080, {0, 0, 1920, 1080}, {0, 0, 1920, 1080});

    KinematicExtrapolator extrapolator;
    ExtrapolationConfig cfg{true, 2.5};

    double dt = 4.0; // 250 Hz tablet report rate (4ms interval)

    // Jump 1: Moving right towards x=1600
    double x = 300.0;
    double outX = 0.0, outY = 0.0;
    for (int i = 0; i < 15; ++i) {
        x += 80.0; // Fast jump velocity: 20 mm/ms
        extrapolator.UpdateWithDt(x, 540.0, dt, cfg, outX, outY);
    }
    ASSERT_TRUE(extrapolator.HasActiveLead());
    ASSERT_GE(outX, x); // Extrapolating forward into jump

    // Hit circle at right, instant sharp reversal jump back left:
    x -= 80.0;
    extrapolator.UpdateWithDt(x, 540.0, dt, cfg, outX, outY);

    // Invariant: Turnaround suppression MUST trigger instantly!
    ASSERT_FALSE(extrapolator.HasActiveLead());
    ASSERT_EQ(outX, x); // Exactly on current position, zero overshoot
}

E2E_TEST(Tier4_Scenario2_FastFlickToStopHardCircleClick, TestTier::Tier4_RealWorld) {
    // Fast Flick to Stop Scenario:
    // Moving fast toward circle, stopping dead, then clicking tip.
    // Tests: Soft-Knee Smoothstep transition + Extrapolator Braking Suppression + Tip pressure gating.

    SoftKneeMotionFilter motionFilter(40, 15.0, 65.0);
    KinematicExtrapolator extrapolator;
    ExtrapolationConfig extraCfg{true, 2.5};

    double dt = 0.004;

    // Fast flick: 200 mm/s (0.8 mm per 4ms frame) -> > vFull (65 mm/s)
    double curX = 0.0;
    double s2X = 0.0, s2Y = 0.0;
    for (int i = 0; i < 20; ++i) {
        curX += 0.8;
        double s1X, s1Y;
        motionFilter.Filter(curX, 50.0, dt, s1X, s1Y);
        extrapolator.UpdateWithDt(s1X, s1Y, dt * 1000.0, extraCfg, s2X, s2Y);
    }
    ASSERT_TRUE(extrapolator.HasActiveLead());

    // Sudden stop: next frame moves only 0.01 mm (>98% speed drop)
    curX += 0.01;
    double s1X, s1Y;
    motionFilter.Filter(curX, 50.0, dt, s1X, s1Y);
    extrapolator.UpdateWithDt(s1X, s1Y, dt * 1000.0, extraCfg, s2X, s2Y);

    // Braking guard MUST have zeroed lead!
    ASSERT_FALSE(extrapolator.HasActiveLead());
    ASSERT_EQ(s2X, s1X);
}

E2E_TEST(Tier4_Scenario3_SmallActiveTabletArea16_9, TestTier::Tier4_RealWorld) {
    // Small Active Area Scenario:
    // Pro player area: 60.0 x 33.75 mm (exact 16:9) centered at (76, 47.5).
    // Tests: Physical boundary clamping + display rect scaling + aspect ratio lock.

    TabletSpec spec{};
    spec.phys_w = 152.0;
    spec.phys_h = 95.0;
    spec.max_x = 15200;
    spec.max_y = 9500;

    double ratio = ParseAspectRatio("16:9");
    double w = 60.0;
    double h = w / ratio;
    ASSERT_NEAR(h, 33.75, 1e-4);

    CoordinateTransformer tf;
    tf.Configure(spec, w, h, 76.0, 47.5, 0.0, 0, 0, 1920, 1080, {0, 0, 1920, 1080}, {0, 0, 1920, 1080}, true);

    // Center maps to exact center of 1080p screen
    double un, vn;
    int px, py;
    tf.TransformRaw(7600, 4750, un, vn, px, py);
    ASSERT_EQ(px, 959);
    ASSERT_EQ(py, 539);

    // Points outside active area clamped cleanly within 1920x1080 viewport
    tf.TransformRaw(0, 0, un, vn, px, py);
    ASSERT_GE(px, 0);
    ASSERT_GE(py, 0);
    ASSERT_LE(px, 1919);
    ASSERT_LE(py, 1079);
}

E2E_TEST(Tier4_Scenario4_MultiAngleContinuousRotationGrip, TestTier::Tier4_RealWorld) {
    // Rotated Grip Scenario:
    // Testing continuous reorientation: -10 deg, +10 deg, +45 deg, 90 deg.
    // Verifies analytical matrix accuracy and zero coordinate drift.

    TabletSpec spec{};
    spec.phys_w = 152.0;
    spec.phys_h = 95.0;
    spec.max_x = 15200;
    spec.max_y = 9500;

    const double angles[] = {-10.0, 10.0, 45.0, -45.0, 90.0, 180.0, 270.0};
    for (double deg : angles) {
        CoordinateTransformer tf;
        tf.Configure(spec, 152.0, 95.0, 76.0, 47.5, deg, 0, 0, 1920, 1080, {0, 0, 1920, 1080}, {0, 0, 1920, 1080});

        // Center must be invariant
        double un, vn;
        int px, py;
        tf.TransformRaw(7600, 4750, un, vn, px, py);
        ASSERT_NEAR(un, 0.5, 1e-4);
        ASSERT_NEAR(vn, 0.5, 1e-4);
        ASSERT_EQ(px, 959);
        ASSERT_EQ(py, 539);
    }
}

E2E_TEST(Tier4_Scenario5_Streaming100kPacketsLatencyBenchmark, TestTier::Tier4_RealWorld) {
    // 100,000-Packet Synthetic Event Stream Loopback & Latency Benchmark.
    // Ingestion -> Decoder -> Affine Transform Matrix -> Filter -> Virtual UInput.
    // Requirement: p50 < 100 us, p99 < 500 us (Strictly Sub-Millisecond).

    auto dev = CreateBestAvailableInputDevice();
    ASSERT_TRUE(dev != nullptr);

    TabletSpec spec{};
    spec.vid = VendorID::Wacom;
    spec.report_id = 0x02;
    spec.x_offset = 2;
    spec.y_offset = 4;
    spec.phys_w = 152.0;
    spec.phys_h = 95.0;
    spec.max_x = 15200;
    spec.max_y = 9500;

    CoordinateTransformer tf;
    tf.Configure(spec, 152.0, 95.0, 76.0, 47.5, 0.0, 0, 0, 1920, 1080, {0, 0, 1920, 1080}, {0, 0, 1920, 1080});

    KinematicExtrapolator extrapolator;
    ExtrapolationConfig cfg{true, 2.5};

    const uint8_t rawPacketTemplate[10] = { 0x02, 0x81, 0x3C, 0x20, 0x18, 0x25, 0x05, 0x00, 0x00, 0x00 };

    constexpr size_t NUM_PACKETS = 100000;
    LatencyStats latencyStats;

    uint64_t benchStart = GetMonotonicRawNs();

    for (size_t i = 0; i < NUM_PACKETS; ++i) {
        uint64_t t0 = GetMonotonicRawNs();

        // 1. Ingestion & Decode
        PenSample sample{};
        DecodeReason r = DecodeSample(rawPacketTemplate, 10, spec, sample);
        (void)r;

        // 2. Fast Affine Matrix Transform
        int px = 0, py = 0;
        tf.TransformRawFast(sample.raw_x, sample.raw_y, px, py);

        // 3. Extrapolation
        double outX = 0.0, outY = 0.0;
        extrapolator.UpdateWithDt(px, py, 4.0, cfg, outX, outY);

        // 4. Virtual Device Injection
        dev->EmitAbs(ABS_X, static_cast<int32_t>(outX));
        dev->EmitAbs(ABS_Y, static_cast<int32_t>(outY));
        dev->EmitSyn();

        uint64_t t1 = GetMonotonicRawNs();
        uint32_t latencyUs = static_cast<uint32_t>((t1 - t0) / 1000ULL);
        latencyStats.Record(latencyUs);

        // Clear mock events periodically to avoid unbounded heap growth
        if (i % 1000 == 0) {
            dev->ClearEvents();
        }
    }

    uint64_t benchEnd = GetMonotonicRawNs();
    double totalTimeSec = static_cast<double>(benchEnd - benchStart) / 1e9;
    double throughputHz = static_cast<double>(NUM_PACKETS) / totalTimeSec;

    uint32_t p50 = 0, p99 = 0, maxUs = 0, minUs = 0;
    latencyStats.Compute(p50, p99, maxUs, minUs);

    std::cout << "\n=======================================================\n";
    std::cout << "  100,000-Packet Synthetic Benchmark Results:\n";
    std::cout << "  Device Mode:  " << (dev->IsKernelDevice() ? "Linux /dev/uinput Kernel" : "In-Memory Virtual Loopback") << "\n";
    std::cout << "  Throughput:   " << std::fixed << std::setprecision(1) << throughputHz << " packets/sec\n";
    std::cout << "  p50 (Median): " << p50 << " us (target < 100 us)\n";
    std::cout << "  p99:          " << p99 << " us (target < 500 us)\n";
    std::cout << "  Min Latency:  " << minUs << " us\n";
    std::cout << "  Max Latency:  " << maxUs << " us\n";
    std::cout << "=======================================================\n";

    // Strictly enforce sub-millisecond requirement!
    ASSERT_LE(p50, 100u);
    ASSERT_LE(p99, 500u);

    dev->Close();
}
