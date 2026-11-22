#include "osupoint/ipc_protocol.h"
#include "osupoint/tablet_spec.h"
#include "osupoint/tablet_decoder.h"
#include "osupoint/rt_scheduler.h"
#include "osupoint/transform_pipeline.h"
#include "osupoint/motion_filters.h"
#include "osupoint/config_manager.h"

#include <cstdio>
#include <cassert>
#include <thread>
#include <atomic>
#include <vector>

static void TestSpscRingBasic() {
    std::printf("[test] Running SpscRing basic tests...\n");
    SpscRing<RawPacket, 64> ring;

    assert(ring.Empty());
    assert(ring.Available() == 0);

    RawPacket p1{};
    p1.len = 10;
    p1.timestamp_ns = 123456789;
    p1.data[0] = 0x02;
    p1.data[1] = 0xAA;

    assert(ring.TryPush(p1));
    assert(!ring.Empty());
    assert(ring.Available() == 1);

    RawPacket out{};
    assert(ring.TryPop(out));
    assert(out.len == 10);
    assert(out.timestamp_ns == 123456789);
    assert(out.data[0] == 0x02);
    assert(out.data[1] == 0xAA);
    assert(ring.Empty());

    // Fill ring to capacity (64 - 1 = 63 or 64)
    size_t count = 0;
    while (ring.TryPush(p1)) {
        count++;
    }
    assert(count == 64);
    assert(!ring.TryPush(p1)); // Full

    for (size_t i = 0; i < count; ++i) {
        assert(ring.TryPop(out));
    }
    assert(ring.Empty());
    std::printf("[test] SpscRing basic tests PASSED\n");
}

static void TestSpscRingConcurrency() {
    std::printf("[test] Running SpscRing concurrency stress test (100k items)...\n");
    SpscRing<RawPacket, 64> ring;
    constexpr uint64_t kTotalItems = 100000;
    std::atomic<bool> start{false};

    std::thread producer([&]() {
        while (!start.load(std::memory_order_acquire)) {}
        for (uint64_t i = 1; i <= kTotalItems; ++i) {
            RawPacket p{};
            p.timestamp_ns = i;
            while (!ring.TryPush(p)) {
                OSU_CPU_PAUSE();
            }
        }
    });

    std::thread consumer([&]() {
        while (!start.load(std::memory_order_acquire)) {}
        uint64_t last_val = 0;
        RawPacket p{};
        while (last_val < kTotalItems) {
            if (ring.TryPop(p)) {
                assert(p.timestamp_ns == last_val + 1);
                last_val = p.timestamp_ns;
            } else {
                OSU_CPU_PAUSE();
            }
        }
        assert(last_val == kTotalItems);
    });

    start.store(true, std::memory_order_release);
    producer.join();
    consumer.join();
    assert(ring.Empty());
    std::printf("[test] SpscRing concurrency stress test PASSED\n");
}

static void TestSeqlock() {
    std::printf("[test] Running Seqlock consistency tests...\n");
    ShmConfigBlock block{};

    TransformConfig cfg{};
    cfg.width_mm = 120.0f;
    cfg.height_mm = 80.0f;
    cfg.cx = 60.0f;
    cfg.cy = 40.0f;
    cfg.rotation_deg = 15.0f;

    WriteConfig(block, cfg);

    TransformConfig read_cfg{};
    assert(ReadConfig(block, read_cfg));
    assert(read_cfg.width_mm == 120.0f);
    assert(read_cfg.height_mm == 80.0f);
    assert(read_cfg.rotation_deg == 15.0f);

    // Concurrency test for Seqlock
    std::atomic<bool> run_seqlock{true};
    std::atomic<uint64_t> reads_completed{0};

    // Reset block to consistent state (height = width * 2) before concurrent test
    TransformConfig initial_c{};
    initial_c.width_mm = 0.0f;
    initial_c.height_mm = 0.0f;
    WriteConfig(block, initial_c);

    std::thread writer([&]() {
        for (int i = 0; i < 50000; ++i) {
            TransformConfig c{};
            c.width_mm = static_cast<float>(i);
            c.height_mm = static_cast<float>(i * 2);
            WriteConfig(block, c);
        }
        run_seqlock.store(false, std::memory_order_release);
    });

    std::thread reader([&]() {
        TransformConfig c{};
        while (run_seqlock.load(std::memory_order_acquire)) {
            if (ReadConfig(block, c)) {
                // Verify consistency: height should be exactly width * 2
                assert(c.height_mm == c.width_mm * 2.0f);
                reads_completed.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    writer.join();
    reader.join();
    std::printf("[test] Seqlock consistency tests PASSED (%zu concurrent reads verified)\n", reads_completed.load());
}

static void TestTabletDecoders() {
    std::printf("[test] Running Multi-Vendor Decoder tests...\n");

    // 1. Wacom CTL-472 tip down packet
    {
        RuntimeTabletSpec spec{};
        spec.vid = 0x056A;
        spec.pid = 0x037A;
        spec.max_x = 15200;
        spec.max_y = 9500;
        spec.report_id = 0x02;
        spec.coord_bits = 16;
        spec.x_offset = 2;
        spec.y_offset = 4;

        // Byte 0: 0x02, Byte 1: 0x21 (in-range + pressed), X=1000, Y=2000, Pressure=1024
        uint8_t pkt[10] = { 0x02, 0x21, 0xE8, 0x03, 0xD0, 0x07, 0x00, 0x04, 0x00, 0x00 };
        PenSample out{};
        DecodeReason r = DecodeTabletPacket(pkt, sizeof(pkt), spec, out);
        assert(r == DecodeReason::Ok);
        assert(out.raw_x == 1000);
        assert(out.raw_y == 2000);
        assert(out.pressure == 1024);
        assert((out.flags & PEN_FLAG_PRESSED) != 0);
        assert((out.flags & PEN_FLAG_IN_RANGE) != 0);
    }

    // 2. Huion control packet rejection (0xE0)
    {
        RuntimeTabletSpec spec{};
        spec.vid = 0x256C;
        spec.pid = 0x006D;
        spec.report_id = 0x08;

        uint8_t pkt[12] = { 0x08, 0xE1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        PenSample out{};
        DecodeReason r = DecodeTabletPacket(pkt, sizeof(pkt), spec, out);
        assert(r == DecodeReason::ControlPacket);
    }

    // 3. VEIKK control packet rejection (0x42)
    {
        RuntimeTabletSpec spec{};
        spec.vid = 0x2FEB;
        spec.pid = 0x0001;

        uint8_t pkt[9] = { 0x02, 0x42, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        PenSample out{};
        DecodeReason r = DecodeTabletPacket(pkt, sizeof(pkt), spec, out);
        assert(r == DecodeReason::ControlPacket);
    }

    // 4. Lift signature handling
    {
        RuntimeTabletSpec spec{};
        spec.vid = 0x056A;
        spec.pid = 0x037A;
        uint8_t pkt[10] = { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
        PenSample out{};
        DecodeReason r = DecodeTabletPacket(pkt, sizeof(pkt), spec, out);
        assert(r == DecodeReason::LiftSignature);
    }

    std::printf("[test] Multi-Vendor Decoder tests PASSED\n");
}

static void TestDecoderBoundsSafety() {
    std::printf("[test] Running Decoder Bounds Safety & UBSan shift tests...\n");

    // 1. Wacom 24-bit with 7-byte report (OOB heap read prevention)
    {
        RuntimeTabletSpec spec{};
        spec.vid = 0x056A;
        spec.report_id = 0x10;
        spec.x_offset = 2;
        spec.y_offset = 5;
        spec.coord_bits = 24;

        uint8_t pkt[7] = { 0x10, 0x81, 0x10, 0x27, 0x01, 0x20, 0x4E };
        PenSample out{};
        DecodeReason r = DecodeTabletPacket(pkt, sizeof(pkt), spec, out);
        assert(r == DecodeReason::ShortReport);
    }

    // 2. Generic 24-bit with 7-byte report
    {
        RuntimeTabletSpec spec{};
        spec.vid = 0x9999;
        spec.report_id = 0x10;
        spec.x_offset = 2;
        spec.y_offset = 5;
        spec.coord_bits = 24;

        uint8_t pkt[7] = { 0x10, 0x81, 0x10, 0x27, 0x01, 0x20, 0x4E };
        PenSample out{};
        DecodeReason r = DecodeTabletPacket(pkt, sizeof(pkt), spec, out);
        assert(r == DecodeReason::ShortReport);
    }

    // 3. Huion truncated packet (< 6 bytes)
    {
        RuntimeTabletSpec spec{};
        spec.vid = 0x256C;
        spec.report_id = 0x08;
        spec.coord_bits = 16;

        uint8_t pkt[5] = { 0x08, 0x81, 0x20, 0x7C, 0x50 };
        PenSample out{};
        DecodeReason r = DecodeTabletPacket(pkt, sizeof(pkt), spec, out);
        assert(r == DecodeReason::ShortReport);
    }

    // 4. XP-Pen truncated packet (< 6 bytes)
    {
        RuntimeTabletSpec spec{};
        spec.vid = 0x28BD;
        spec.report_id = 0x02;
        spec.coord_bits = 16;

        uint8_t pkt[5] = { 0x02, 0x81, 0x64, 0x09, 0x40 };
        PenSample out{};
        DecodeReason r = DecodeTabletPacket(pkt, sizeof(pkt), spec, out);
        assert(r == DecodeReason::ShortReport);
    }

    // 5. VEIKK truncated packet (< 7 bytes)
    {
        RuntimeTabletSpec spec{};
        spec.vid = 0x2FEB;
        spec.report_id = 0x02;
        spec.coord_bits = 16;

        uint8_t pkt[6] = { 0x02, 0x41, 0xA0, 0x64, 0x09, 0x40 };
        PenSample out{};
        DecodeReason r = DecodeTabletPacket(pkt, sizeof(pkt), spec, out);
        assert(r == DecodeReason::ShortReport);
    }

    // 6. Shift overflow protection when coord_bits >= 31
    {
        RuntimeTabletSpec spec{};
        spec.vid = 0x056A;
        spec.report_id = 0x02;
        spec.coord_bits = 31;
        spec.max_x = 0;
        spec.max_y = 0;

        uint8_t pkt[10] = { 0x02, 0x81, 0x3C, 0x20, 0x18, 0x25, 0x05, 0x00, 0x00, 0x00 };
        PenSample out{};
        DecodeReason r = DecodeTabletPacket(pkt, sizeof(pkt), spec, out);
        assert(r == DecodeReason::Ok);

        spec.coord_bits = 32;
        r = DecodeTabletPacket(pkt, sizeof(pkt), spec, out);
        assert(r == DecodeReason::Ok);
    }

    std::printf("[test] Decoder Bounds Safety & UBSan shift tests PASSED\n");
}

static void TestTimingMonotonic() {
    std::printf("[test] Running High-Resolution Timing test...\n");
    uint64_t t1 = GetMonotonicTimeNs();
    usleep(1000); // 1 ms
    uint64_t t2 = GetMonotonicTimeNs();
    assert(t2 > t1);
    uint64_t delta_us = (t2 - t1) / 1000;
    assert(delta_us >= 500 && delta_us <= 10000);
    std::printf("[test] High-Resolution Timing test PASSED (measured ~%lu us)\n", delta_us);
}

static void TestTransformPipeline() {
    std::printf("[test] Running Transform Pipeline tests...\n");

    RuntimeTabletSpec spec{};
    spec.phys_w = 152.0;
    spec.phys_h = 95.0;
    spec.max_x = 15200;
    spec.max_y = 9500;

    CoordinateTransformer tf;
    tf.Configure(spec, 152.0, 95.0, 76.0, 47.5, 0.0, 0, 0, 1920, 1080, {0, 0, 1920, 1080}, {0, 0, 1920, 1080});

    double un = 0.0, vn = 0.0;
    int px = 0, py = 0;
    tf.TransformRaw(0, 0, un, vn, px, py);
    assert(px == 0 && py == 0);

    tf.TransformRaw(15200, 9500, un, vn, px, py);
    assert(px == 1919 && py == 1079);

    int px_fast = 0, py_fast = 0;
    tf.TransformRawFast(15200, 9500, px_fast, py_fast);
    assert(px_fast == px && py_fast == py);

    // Aspect ratio parsing tests
    assert(std::abs(ParseAspectRatio("16:9") - (16.0 / 9.0)) < 1e-4);
    assert(std::abs(ParseAspectRatio("16:10") - (1.6)) < 1e-4);
    assert(std::abs(ParseAspectRatio("16x9") - (16.0 / 9.0)) < 1e-4);
    assert(std::abs(ParseAspectRatio("16/9") - (16.0 / 9.0)) < 1e-4);

    double out_ratio = 0.0;
    std::string ratio_str = MatchMonitorAspectRatio(1920, 1080, out_ratio);
    assert(ratio_str == "16:9");
    assert(std::abs(out_ratio - (16.0 / 9.0)) < 1e-4);

    std::printf("[test] Transform Pipeline tests PASSED\n");
}

static void TestMotionFilters() {
    std::printf("[test] Running Motion Filters & Cascade tests...\n");

    OneEuroFilter2D euro;
    euro.Configure(50);
    double ox = 0.0, oy = 0.0;
    euro.Filter(100.0, 50.0, 0.004, ox, oy);
    assert(std::abs(ox - 100.0) < 1e-4);

    SoftKneeMotionFilter knee(40);
    double kx = 0.0, ky = 0.0;
    knee.Filter(100.0, 50.0, 0.004, kx, ky);
    assert(std::abs(kx - 100.0) < 1e-4);

    KinematicExtrapolator extra;
    ExtrapolationConfig cfg{true, 2.5};
    double ex = 0.0, ey = 0.0;
    extra.UpdateWithDt(100.0, 50.0, 4.0, cfg, ex, ey);
    assert(std::abs(ex - 100.0) < 1e-4);

    FilterCascade cascade;
    TransformConfig tc{};
    tc.filter_1euro = 30;
    tc.filter_motion = 40;
    tc.extrapolation = 1;
    tc.extrapolation_tau = 2.5f;
    cascade.UpdateConfig(tc);

    double cx = 0.0, cy = 0.0;
    cascade.Process(100.0, 50.0, 0.004, cx, cy);
    assert(std::abs(cx - 100.0) < 1e-4);

    std::printf("[test] Motion Filters & Cascade tests PASSED\n");
}

static void TestConfigManager() {
    std::printf("[test] Running Config Manager & Serialization tests...\n");

    std::string ini = "# Test config\n"
                      "width=120.5\n"
                      "height=75.3\n"
                      "center_x=76.0\n"
                      "center_y=47.5\n"
                      "rotation=15.0\n"
                      "ratio=\"16:9\"\n"
                      "lock_ratio=1\n"
                      "clip_area=true\n"
                      "extrapolation_mm=3.0\n"
                      "filter=40\n"
                      "motion_filter=50\n"
                      "spin_percent=25\n"
                      "\n[preset:TestPreset]\n"
                      "width=100.0\n"
                      "height=60.0\n"
                      "ratio=16:10\n"
                      "\n[palette:TestPalette]\n"
                      "accent=#112233\n";

    OsuPointConfig cfg = ConfigManager::ParseIni(ini);
    assert(std::abs(cfg.width - 120.5) < 1e-4);
    assert(std::abs(cfg.height - 75.3) < 1e-4);
    assert(cfg.lock_ratio == true);
    assert(cfg.clip_area == true);
    assert(cfg.filter == 40);
    assert(cfg.motion_filter == 50);
    assert(cfg.spin_percent == 25);
    assert(cfg.presets.find("TestPreset") != cfg.presets.end());
    assert(cfg.palettes.find("TestPalette") != cfg.palettes.end());

    std::string serialized = ConfigManager::SerializeIni(cfg);
    OsuPointConfig roundtrip = ConfigManager::ParseIni(serialized);
    assert(std::abs(roundtrip.width - 120.5) < 1e-4);
    assert(std::abs(roundtrip.height - 75.3) < 1e-4);
    assert(roundtrip.filter == 40);
    assert(roundtrip.motion_filter == 50);

    TransformConfig tc{};
    ConfigManager::ToTransformConfig(cfg, tc);
    assert(tc.filter_1euro == 40);
    assert(tc.filter_motion == 50);
    assert(tc.spin_percent == 25);
    assert(tc.clip_area == 1);

    std::printf("[test] Config Manager & Serialization tests PASSED\n");
}

int main() {
    std::printf("=========================================\n");
    std::printf("osuPoint Linux Driver Core Test Harness\n");
    std::printf("=========================================\n");

    TestSpscRingBasic();
    TestSpscRingConcurrency();
    TestSeqlock();
    TestTabletDecoders();
    TestDecoderBoundsSafety();
    TestTimingMonotonic();
    TestTransformPipeline();
    TestMotionFilters();
    TestConfigManager();

    std::printf("=========================================\n");
    std::printf("ALL TEST SUITES PASSED CLEANLY (9/9)\n");
    std::printf("=========================================\n");
    return 0;
}
