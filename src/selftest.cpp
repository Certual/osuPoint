#include "log.h"
#include <vector>
#include <string>
#include <cstdint>
#include "tablet_spec.h"
#include "driver_pipeline.h"

#undef ERROR // Prevent conflict with LogLevel::ERROR

struct GoldenVector {
    std::string name;
    std::vector<uint8_t> data;
    bool expected_valid;
    int expected_x;
    int expected_y;
    int expected_pressure;
};

// Reference golden vectors for regression testing of the decoder
const std::vector<GoldenVector> test_vectors = {
    { "V1 - Wacom CTL-472 Valid Tip", { 0x02, 0x81, 0x3C, 0x20, 0x18, 0x25, 0x05, 0x00, 0x00, 0x00 }, true, 8252, 9496, 5 },
    { "V2 - Wacom Hover",             { 0x02, 0x41, 0x3C, 0x20, 0x18, 0x25, 0x00, 0x00, 0x00, 0x00 }, true, 8252, 9496, 0 },
    { "V3 - Wacom 0x10",              { 0x10, 0x81, 0x3C, 0x20, 0x18, 0x25, 0x05, 0x00, 0x00, 0x00 }, true, 8252, 9496, 5 },
    { "V4 - Huion 0x08",              { 0x08, 0x81, 0x20, 0x7C, 0x50, 0x4E, 0x1F, 0x40, 0x00, 0x00, 0x00, 0x00 }, true, 0x7C20, 0x4E50, 0x401F },
    { "V5 - XP-Pen 12B",              { 0x02, 0x81, 0x64, 0x09, 0x40, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00 }, true, 0x0964, 0x0640, 7 },
    { "V6 - XP-Pen 12B bit 0x10",     { 0x02, 0x91, 0x64, 0x09, 0x40, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V7 - XP-Pen lift",             { 0x02, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V8 - VEIKK 9B",                { 0x02, 0x41, 0xA1, 0x64, 0x09, 0x40, 0x06, 0x07, 0x00, 0x00 }, true, 0x0964, 0x0640, 7 },
    { "V9 - Generic wrong id",        { 0x03, 0x81, 0x3C, 0x20, 0x18, 0x25, 0x05, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V10 - 17-bit Huion",           { 0x08, 0x81, 0x20, 0x7C, 0x50, 0x4E, 0x1F, 0x40, 0x00, 0x00, 0x00, 0x00 }, true, 0x7C20, 0x4E50, 0x401F },
    { "V11 - zero-frame",             { 0x08, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V12 - hover (0,0) in-range",   { 0x02, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, true, 0, 0, 0 },
    { "V13 - Wacom Custom FW Tip",    { 0x02, 0xC0, 0xBE, 0x1C, 0x72, 0x0C, 0x10, 0x00, 0x00, 0x00 }, true, 7358, 3186, 16 },
    { "V14 - Wacom Hover Dist Byte8", { 0x02, 0x40, 0x3C, 0x20, 0x18, 0x25, 0x00, 0x00, 0x35, 0x00 }, true, 8252, 9496, 0 },
    { "V15 - Wacom Barrel Button 1",  { 0x02, 0x42, 0x3C, 0x20, 0x18, 0x25, 0x00, 0x00, 0x00, 0x00 }, true, 8252, 9496, 0 },
    { "V16 - Wacom Barrel Button 2",  { 0x02, 0x44, 0x3C, 0x20, 0x18, 0x25, 0x00, 0x00, 0x00, 0x00 }, true, 8252, 9496, 0 },
    { "V17 - Huion 17-bit with Byte8 High Bits", { 0x08, 0x81, 0x20, 0x7C, 0x50, 0x4E, 0x1F, 0x40, 0x03, 0x00, 0x00, 0x00 }, true, 0x17C20, 0x14E50, 0x401F },
    { "V18 - Wacom Bluetooth Tip",    { 0x82, 0x81, 0x3C, 0x20, 0x18, 0x25, 0x05, 0x00, 0x00, 0x00 }, true, 8252, 9496, 5 },
    { "V19 - Wacom Bluetooth Hover",  { 0xC2, 0x41, 0x3C, 0x20, 0x18, 0x25, 0x00, 0x00, 0x00, 0x00 }, true, 8252, 9496, 0 },
    { "V20 - Wacom Bluetooth Lift",   { 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V21 - Wacom X Coord 0xC0 Valid", { 0x02, 0x41, 0xC0, 0x20, 0x18, 0x25, 0x00, 0x00, 0x00, 0x00 }, true, 0x20C0, 9496, 0 },
    { "V22 - Huion ExpressKey Reject (0xE0)", { 0x08, 0xE0, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V23 - Huion TouchStrip Reject (0xF0)", { 0x08, 0xF0, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V24 - VEIKK ExpressKey Reject (0x42)", { 0x02, 0x42, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V25 - VEIKK Pen Lift (0x41 0xC0)",     { 0x02, 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V26 - XP-Pen 24-bit High-Res",        { 0x02, 0x81, 0x10, 0x27, 0x20, 0x4E, 0xFF, 0x1F, 0x01, 0x01, 0x00, 0x00 }, true, 0x12710, 0x14E20, 0x1FFF },
    { "V27 - Wacom Pro 24-bit (0x10)",       { 0x10, 0x81, 0x10, 0x27, 0x01, 0x20, 0x4E, 0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, true, 0x12710, 0x14E20, 5 },
    { "V28 - XP-Pen Hover 0x80 Non-Zero",     { 0x02, 0x80, 0x64, 0x09, 0x40, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, true, 0x0964, 0x0640, 0 },
    { "V29 - Huion Pen Lift 0x00 Reject",     { 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V30 - Artisul Zero-Frame Lift Reject", { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
    { "V31 - VEIKK Invalid Status 0x00",      { 0x02, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, false, 0, 0, 0 },
};



static void RunTransformTests();
static void RunExtrapolationTests();
void RunSelfTest() {
    ous_log(LogLevel::INFO, "SelfTst", "Running selftest with V1-V31 Reference Golden Vectors");
    int passed = 0;
    for (const auto& v : test_vectors) {
        TabletSpec spec{};
        if (v.name.find("VEIKK") != std::string::npos) {
            spec.vid = 0x2FEB;
        } else if (v.name.find("XP-Pen") != std::string::npos) {
            spec.vid = 0x28BD;
            spec.report_id = 0x02;
            spec.x_offset = 2;
            spec.y_offset = 4;
            if (v.name.find("24-bit") != std::string::npos) {
                spec.coord_bits = 24;
                spec.max_x = 200000;
                spec.max_y = 200000;
            }
        } else if (v.name.find("Huion") != std::string::npos || v.name.find("V11") != std::string::npos) {
            spec.vid = 0x256C;
            spec.report_id = 0x08;
            spec.x_offset = 2;
            spec.y_offset = 4;
            if (v.name.find("17-bit") != std::string::npos) {
                spec.coord_bits = 17;
            }
        } else if (v.name.find("Artisul") != std::string::npos) {
            spec.vid = VendorID::Artisul;
            spec.report_id = 0x02;
            spec.x_offset = 2;
            spec.y_offset = 4;
        } else if (v.name.find("Wacom") != std::string::npos || v.name.find("CTL") != std::string::npos || v.name.find("Invalid") != std::string::npos || v.name.find("Extreme") != std::string::npos || v.name.find("Short") != std::string::npos || v.name.find("Missing") != std::string::npos || v.name.find("V9") != std::string::npos || v.name.find("V12") != std::string::npos) {
            spec.vid = 0x056A;
            spec.report_id = 0x02;
            spec.x_offset = 2;
            spec.y_offset = 4;
            if (v.name.find("24-bit") != std::string::npos) {
                spec.coord_bits = 24;
                spec.report_id = 0x10;
                spec.y_offset = 5;
            }
        }
        
        spec.max_x = 2000000;
        spec.max_y = 2000000;

        int32_t out_x = 0, out_y = 0;
        PenSample out_sample{};
        DecodeReason reason = DecodeSample(v.data.data(), (int)v.data.size(), spec, out_sample);
        bool result = (reason == DecodeReason::Ok);
        out_x = out_sample.raw_x;
        out_y = out_sample.raw_y;
        
        if (result == v.expected_valid) {
            bool mismatch = false;
            if (result) {
                if (out_x != v.expected_x || out_y != v.expected_y) {
                    ous_log(LogLevel::ERROR, "SelfTst", "Failed %s: expected coords (%d,%d) got (%d,%d)", v.name.c_str(), v.expected_x, v.expected_y, out_x, out_y);
                    mismatch = true;
                }
                if (out_sample.pressure != (uint16_t)v.expected_pressure) {
                    ous_log(LogLevel::ERROR, "SelfTst", "Failed %s: expected pressure %d got %d", v.name.c_str(), v.expected_pressure, (int)out_sample.pressure);
                    mismatch = true;
                }
                bool expected_pressed = (v.expected_pressure > 0);
                bool actual_pressed = (out_sample.flags & 1) != 0;
                if (expected_pressed != actual_pressed) {
                    ous_log(LogLevel::ERROR, "SelfTst", "Failed %s: expected pressed=%d got %d (flags=0x%02X)", v.name.c_str(), expected_pressed, actual_pressed, out_sample.flags);
                    mismatch = true;
                }
                if (v.name.find("Barrel") != std::string::npos && (out_sample.flags & 4) == 0) {
                    ous_log(LogLevel::ERROR, "SelfTst", "Failed %s: expected barrel flag 0x04 got flags=0x%02X", v.name.c_str(), out_sample.flags);
                    mismatch = true;
                }
            }
            if (!mismatch) {
                ous_log(LogLevel::INFO, "SelfTst", "Passed %s", v.name.c_str());
                passed++;
            }
        } else {
            ous_log(LogLevel::ERROR, "SelfTst", "Failed %s: expected valid %d got %d (reason=%d, out_x=%d, out_y=%d)", v.name.c_str(), v.expected_valid, result, (int)reason, out_x, out_y);
        }
    }
    ous_log(LogLevel::INFO, "SelfTst", "Self test completed: %d/%d vectors passed.", passed, (int)test_vectors.size());

    static auto runThresholdTests = []() {
        TabletSpec spec{};
        spec.vid = 0x056A;
        spec.report_id = 0x02;
        spec.x_offset = 2;
        spec.y_offset = 4;
        spec.tip_pressure_threshold = 20;

        uint8_t repBelow[10] = { 0x02, 0x81, 0x3C, 0x20, 0x18, 0x25, 0x0A, 0x00, 0x00, 0x00 };
        PenSample sampleBelow{};
        DecodeSample(repBelow, 10, spec, sampleBelow);
        if ((sampleBelow.flags & 1) != 0) {
            ous_log(LogLevel::ERROR, "SelfTst", "Failed threshold test: pressure 10 >= threshold 20 asserted tip");
        }

        uint8_t repAbove[10] = { 0x02, 0x81, 0x3C, 0x20, 0x18, 0x25, 0x19, 0x00, 0x00, 0x00 };
        PenSample sampleAbove{};
        DecodeSample(repAbove, 10, spec, sampleAbove);
        if ((sampleAbove.flags & 1) == 0) {
            ous_log(LogLevel::ERROR, "SelfTst", "Failed threshold test: pressure 25 >= threshold 20 did not assert tip");
        }
        ous_log(LogLevel::INFO, "SelfTst", "Tip pressure threshold gating tests completed.");
    };
    runThresholdTests();

    RunTransformTests();
    RunExtrapolationTests();
}

#include <cmath>

extern int g_disp_x, g_disp_y, g_disp_w, g_disp_h;

static void RunTransformTests() {
    ous_log(LogLevel::INFO, "SelfTst", "Running golden-transform tests (sec 3.5)");
    
    // Prepare global variables for calculation
    g_spec.max_x = 15200;
    g_spec.max_y = 9500;
    g_spec.phys_w = 152.0;
    g_spec.phys_h = 95.0;
    
    g_disp_x = 0;
    g_disp_y = 0;
    g_disp_w = 1920;
    g_disp_h = 1080;
    
    // Test 1: Zero rotation
    SetTransform(152.0, 95.0, 76.0, 47.5, 0.0, 0);
    TransformConfig cfg = g_cfgStore.Load();
    
    auto apply_transform = [&](int raw_x, int raw_y, double& un, double& vn, int& px, int& py) {
        double vfx = raw_x;
        double vfy = raw_y;
        un = vfx * cfg.knx[0] + vfy * cfg.kny[0] + cfg.knc[0];
        vn = vfx * cfg.knx[1] + vfy * cfg.kny[1] + cfg.knc[1];
        
        double out_x = un * cfg.vscale[0] + cfg.vshift[0];
        double out_y = vn * cfg.vscale[1] + cfg.vshift[1];
        
        out_x = out_x < 0 ? 0 : (out_x > 65535 ? 65535 : out_x);
        out_y = out_y < 0 ? 0 : (out_y > 65535 ? 65535 : out_y);
        
        double x_virt = 0.0;
        double y_virt = 0.0;
        double w_virt = 1920.0;
        double h_virt = 1080.0;
        double denom_x = (w_virt > 1.0) ? (w_virt - 1.0) : 1.0;
        double denom_y = (h_virt > 1.0) ? (h_virt - 1.0) : 1.0;
        
        px = (int)((out_x * denom_x) / 65535.0 + x_virt);
        py = (int)((out_y * denom_y) / 65535.0 + y_virt);
    };
    
    double un, vn;
    int px, py;
    
    apply_transform(0, 0, un, vn, px, py);
    if (px != 0 || py != 0) ous_log(LogLevel::ERROR, "SelfTst", "Failed rot=0 (0,0) -> (%d,%d)", px, py);
    
    apply_transform(15200, 9500, un, vn, px, py);
    if (px != 1919 || py != 1079) ous_log(LogLevel::ERROR, "SelfTst", "Failed rot=0 (15200,9500) -> (%d,%d)", px, py);
    
    apply_transform(7600, 4750, un, vn, px, py);
    if (px != 959 || py != 539) ous_log(LogLevel::ERROR, "SelfTst", "Failed rot=0 (7600,4750) -> (%d,%d)", px, py);
    
    // Test 2: Rotate +10 degrees
    SetTransform(152.0, 95.0, 76.0, 47.5, 10.0, 0);
    cfg = g_cfgStore.Load();
    
    apply_transform(15200, 4750, un, vn, px, py);
    double expected_un = 0.5 + std::cos(10.0 * 3.14159265358979323846 / 180.0) / 2.0;
    double expected_vn = 0.5 - std::sin(10.0 * 3.14159265358979323846 / 180.0) * (76.0 / 95.0);
    
    if (std::abs(un - expected_un) > 1e-4 || std::abs(vn - expected_vn) > 1e-4) {
        ous_log(LogLevel::ERROR, "SelfTst", "Failed rot=+10 (15200,4750) -> un=%.5f vn=%.5f (expected un=%.5f vn=%.5f)", un, vn, expected_un, expected_vn);
    }
    ous_log(LogLevel::INFO, "SelfTst", "rot=+10 (15200,4750) -> un=%.5f vn=%.5f px=%d py=%d", un, vn, px, py);

    // Test 3: Rotate -10 degrees
    SetTransform(152.0, 95.0, 76.0, 47.5, -10.0, 0);
    cfg = g_cfgStore.Load();

    apply_transform(15200, 4750, un, vn, px, py);
    double expected_un_neg = 0.5 + std::cos(-10.0 * 3.14159265358979323846 / 180.0) / 2.0;
    double expected_vn_neg = 0.5 - std::sin(-10.0 * 3.14159265358979323846 / 180.0) * (76.0 / 95.0);

    if (std::abs(un - expected_un_neg) > 1e-4 || std::abs(vn - expected_vn_neg) > 1e-4) {
        ous_log(LogLevel::ERROR, "SelfTst", "Failed rot=-10 (15200,4750) -> un=%.5f vn=%.5f (expected un=%.5f vn=%.5f)", un, vn, expected_un_neg, expected_vn_neg);
    }
    ous_log(LogLevel::INFO, "SelfTst", "rot=-10 (15200,4750) -> un=%.5f vn=%.5f px=%d py=%d", un, vn, px, py);

    ous_log(LogLevel::INFO, "SelfTst", "Golden-transform tests completed.");

    // Test: Aspect ratio lock
    extern double ParseAspectRatio(const std::string& str);
    double ratio_val = ParseAspectRatio("16:9");
    if (std::abs(ratio_val - (16.0 / 9.0)) > 1e-4) {
        ous_log(LogLevel::ERROR, "SelfTst", "Failed ParseAspectRatio for 16:9");
    }
    double w_test = 152.0;
    double h_test = w_test / ratio_val;
    if (std::abs(h_test - 85.5) > 1e-4) { // 152 / (16/9) = 152 * 9 / 16 = 85.5
        ous_log(LogLevel::ERROR, "SelfTst", "Failed aspect ratio lock logic h = w / ratio");
    }
    ous_log(LogLevel::INFO, "SelfTst", "Aspect-ratio lock tests completed.");

    // Test: Latency and Jitter calculation
    {
        LatencyStats latTest;
        latTest.Record(10);
        latTest.Record(20);
        latTest.Record(30);
        latTest.Record(40);
        latTest.Record(50);
        uint32_t p50 = 0, p99 = 0, maxUs = 0, minUs = 0;
        latTest.Compute(p50, p99, maxUs, minUs);
        if (p50 != 30 || minUs != 10 || maxUs != 50) {
            ous_log(LogLevel::ERROR, "SelfTst", "LatencyStats failed: p50=%u min=%u max=%u", p50, minUs, maxUs);
        } else {
            ous_log(LogLevel::INFO, "SelfTst", "LatencyStats verification passed (p50=%uus min=%uus max=%uus)", p50, minUs, maxUs);
        }

        JitterStats jitTest;
        jitTest.RecordInterval(7500);
        jitTest.RecordInterval(7520);
        jitTest.RecordInterval(7480);
        jitTest.RecordInterval(7510);
        uint32_t jitVal = jitTest.GetJitterUs();
        uint32_t stdDev = 0, avgIntv = 0;
        jitTest.Compute(stdDev, avgIntv);
        if (jitVal == 0 || stdDev == 0 || avgIntv < 7400 || avgIntv > 7600) {
            ous_log(LogLevel::ERROR, "SelfTst", "JitterStats failed: jit=%u stdDev=%u avg=%u", jitVal, stdDev, avgIntv);
        } else {
            ous_log(LogLevel::INFO, "SelfTst", "JitterStats verification passed (jit=%uus stdDev=%uus avg=%uus)", jitVal, stdDev, avgIntv);
        }
    }
}

static void RunExtrapolationTests() {
    ous_log(LogLevel::INFO, "SelfTst", "Running Adaptive Kinematic Extrapolation tests");

    KinematicExtrapolator ext;
    ExtrapolationConfig cfg;
    double ox = 0.0, oy = 0.0;

    // Test 1: Disabled mode passes through untouched
    cfg.enabled = false;
    cfg.tau_ms = 2.5;
    ext.Reset();
    ext.UpdateWithDt(100.0, 200.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(120.0, 240.0, 4.0, cfg, ox, oy);
    if (ox != 120.0 || oy != 240.0) {
        ous_log(LogLevel::ERROR, "SelfTst", "Extrapolation test failed: disabled mode modified coordinates");
    }

    // Test 2: Stationary jitter deadzone suppression
    cfg.enabled = true;
    cfg.tau_ms = 2.5;
    ext.Reset();
    ext.UpdateWithDt(500.0, 500.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(500.005, 500.002, 4.0, cfg, ox, oy);
    if (std::abs(ox - 500.005) > 1e-6 || std::abs(oy - 500.002) > 1e-6) {
        ous_log(LogLevel::ERROR, "SelfTst", "Extrapolation test failed: micro-jitter deadzone not suppressed");
    }

    // Test 3: Constant velocity forward prediction
    ext.Reset();
    ext.UpdateWithDt(0.0, 0.0, 4.0, cfg, ox, oy);    // p0
    ext.UpdateWithDt(20.0, 0.0, 4.0, cfg, ox, oy);   // p1: v=5.0
    ext.UpdateWithDt(40.0, 0.0, 4.0, cfg, ox, oy);   // p2: v=5.0, a=0.0 -> lead = 5.0 * 2.5 = 12.5 -> out = 52.5
    if (std::abs(ox - 52.5) > 1e-3 || std::abs(oy - 0.0) > 1e-3) {
        ous_log(LogLevel::ERROR, "SelfTst", "Extrapolation test failed: constant velocity expected 52.5 got %.3f", ox);
    }
    ext.UpdateWithDt(60.0, 0.0, 4.0, cfg, ox, oy);   // p3: v=5.0, a=0.0 -> out = 72.5
    if (std::abs(ox - 72.5) > 1e-3 || std::abs(oy - 0.0) > 1e-3) {
        ous_log(LogLevel::ERROR, "SelfTst", "Extrapolation test failed: constant velocity step 2 expected 72.5 got %.3f", ox);
    }

    // Test 4: Turnaround suppression (v_t * v_{t-1} < 0)
    // Moving forward then sharp reversal
    ext.UpdateWithDt(55.0, 0.0, 4.0, cfg, ox, oy);   // p4: reversal vx = -1.25, prev_vx = 5.0 -> dot < 0 -> lead = 0
    if (ox != 55.0 || oy != 0.0) {
        ous_log(LogLevel::ERROR, "SelfTst", "Extrapolation test failed: turnaround suppression failed, expected 55.0 got %.3f", ox);
    }

    // Test 5: True diagonal turnaround suppression (v_t * v_{t-1} < 0)
    ext.Reset();
    ext.UpdateWithDt(0.0, 0.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(10.0, 10.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(20.0, 20.0, 4.0, cfg, ox, oy);  // v=(2.5, 2.5)
    ext.UpdateWithDt(15.0, 15.0, 4.0, cfg, ox, oy);  // v=(-1.25, -1.25) -> dot = -1.25*2.5 + -1.25*2.5 = -6.25 < 0 -> lead = 0
    if (ox != 15.0 || oy != 15.0) {
        ous_log(LogLevel::ERROR, "SelfTst", "Extrapolation test failed: diagonal turnaround suppression failed, expected (15, 15) got (%.1f, %.1f)", ox, oy);
    }

    // Test 6: Orthogonal corner motion (dot == 0.0 is NOT a turnaround)
    ext.Reset();
    ext.UpdateWithDt(0.0, 0.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(20.0, 0.0, 4.0, cfg, ox, oy);   // v=(5.0, 0.0)
    ext.UpdateWithDt(40.0, 0.0, 4.0, cfg, ox, oy);   // v=(5.0, 0.0)
    ext.UpdateWithDt(40.0, 20.0, 4.0, cfg, ox, oy);  // v=(0.0, 5.0) -> dot = 0.0 >= 0 (orthogonal 90-deg turn, continues moving)
    if (oy <= 20.0) {
        ous_log(LogLevel::ERROR, "SelfTst", "Extrapolation test failed: orthogonal turn falsely suppressed upward velocity, got oy=%.3f", oy);
    }

    // Test 7: Acceleration from standstill / stationary jitter (starting a flick from rest must extrapolate forward)
    ext.Reset();
    ext.UpdateWithDt(500.0, 500.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(500.002, 500.001, 4.0, cfg, ox, oy); // stationary deadband (speed < noise)
    ext.UpdateWithDt(520.0, 500.001, 4.0, cfg, ox, oy);   // user starts flick: v=(4.999, 0), accelerating from rest -> MUST extrapolate forward
    if (ox <= 520.0) {
        ous_log(LogLevel::ERROR, "SelfTst", "Extrapolation test failed: accelerating from standstill was falsely suppressed, got ox=%.3f", ox);
    }

    // Test 8: Sharp braking / stopping suppression (hitting a circle)
    ext.Reset();
    ext.UpdateWithDt(0.0, 0.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(40.0, 0.0, 4.0, cfg, ox, oy);   // v=10.0
    ext.UpdateWithDt(80.0, 0.0, 4.0, cfg, ox, oy);   // v=10.0
    ext.UpdateWithDt(81.0, 0.0, 4.0, cfg, ox, oy);   // v=0.25 (speed dropped by 97.5% -> sharp stop -> lead = 0)
    if (ox != 81.0 || oy != 0.0) {
        ous_log(LogLevel::ERROR, "SelfTst", "Extrapolation test failed: sharp braking suppression failed, expected 81.0 got %.3f", ox);
    }

    // Test 9: Tau scaling (1.0 ms and 5.0 ms)
    ext.Reset();
    cfg.tau_ms = 1.0;
    ext.UpdateWithDt(0.0, 0.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(20.0, 0.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(40.0, 0.0, 4.0, cfg, ox, oy);   // v=5.0, tau=1.0 -> lead = 5.0 -> out = 45.0
    if (std::abs(ox - 45.0) > 1e-3) {
        ous_log(LogLevel::ERROR, "SelfTst", "Extrapolation test failed: tau=1.0ms expected 45.0 got %.3f", ox);
    }

    cfg.tau_ms = 5.0;
    ext.Reset();
    ext.UpdateWithDt(0.0, 0.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(20.0, 0.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(40.0, 0.0, 4.0, cfg, ox, oy);   // v=5.0, tau=5.0 -> lead = 25.0 -> out = 65.0
    if (std::abs(ox - 65.0) > 1e-3) {
        ous_log(LogLevel::ERROR, "SelfTst", "Extrapolation test failed: tau=5.0ms expected 65.0 got %.3f", ox);
    }

    // Test 10: Reset clears all momentum
    ext.Reset();
    ext.UpdateWithDt(1000.0, 2000.0, 4.0, cfg, ox, oy);
    if (ox != 1000.0 || oy != 2000.0) {
        ous_log(LogLevel::ERROR, "SelfTst", "Extrapolation test failed: post-reset first sample modified");
    }

    ous_log(LogLevel::INFO, "SelfTst", "Adaptive Kinematic Extrapolation tests completed successfully.");
}

