#include "test_framework.h"
#include "test_vectors.h"
#include "test_pipeline.h"
#include "test_filter_impl.h"
#include "test_config_impl.h"

#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <chrono>

// --- DECODER TESTS (V1 - V31 Golden Reference Vectors) ---

#define DEFINE_VECTOR_TEST(index, name_str) \
E2E_TEST(Decoder_Vector_##index, TestTier::Tier1_Feature) { \
    const auto& v = g_test_vectors[index - 1]; \
    TabletSpec spec = SpecForVector(v); \
    PenSample out_sample{}; \
    DecodeReason reason = DecodeSample(v.data.data(), static_cast<int>(v.data.size()), spec, out_sample); \
    bool result = (reason == DecodeReason::Ok); \
    ASSERT_EQ(result, v.expected_valid); \
    if (result) { \
        ASSERT_EQ(out_sample.raw_x, v.expected_x); \
        ASSERT_EQ(out_sample.raw_y, v.expected_y); \
        ASSERT_EQ(out_sample.pressure, static_cast<uint16_t>(v.expected_pressure)); \
        bool expected_pressed = (v.expected_pressure > 0); \
        bool actual_pressed = (out_sample.flags & 1) != 0; \
        ASSERT_EQ(actual_pressed, expected_pressed); \
        if (v.name.find("Barrel") != std::string::npos) { \
            ASSERT_TRUE((out_sample.flags & 4) != 0); \
        } \
    } \
}

DEFINE_VECTOR_TEST(1, V1_Wacom_CTL472_Valid_Tip)
DEFINE_VECTOR_TEST(2, V2_Wacom_Hover)
DEFINE_VECTOR_TEST(3, V3_Wacom_0x10)
DEFINE_VECTOR_TEST(4, V4_Huion_0x08)
DEFINE_VECTOR_TEST(5, V5_XPPen_12B)
DEFINE_VECTOR_TEST(6, V6_XPPen_12B_Bit0x10)
DEFINE_VECTOR_TEST(7, V7_XPPen_Lift)
DEFINE_VECTOR_TEST(8, V8_VEIKK_9B)
DEFINE_VECTOR_TEST(9, V9_Generic_Wrong_Id)
DEFINE_VECTOR_TEST(10, V10_17bit_Huion)
DEFINE_VECTOR_TEST(11, V11_Zero_Frame)
DEFINE_VECTOR_TEST(12, V12_Hover_0_0_InRange)
DEFINE_VECTOR_TEST(13, V13_Wacom_Custom_FW_Tip)
DEFINE_VECTOR_TEST(14, V14_Wacom_Hover_Dist_Byte8)
DEFINE_VECTOR_TEST(15, V15_Wacom_Barrel_Button1)
DEFINE_VECTOR_TEST(16, V16_Wacom_Barrel_Button2)
DEFINE_VECTOR_TEST(17, V17_Huion_17bit_Byte8_High_Bits)
DEFINE_VECTOR_TEST(18, V18_Wacom_Bluetooth_Tip)
DEFINE_VECTOR_TEST(19, V19_Wacom_Bluetooth_Hover)
DEFINE_VECTOR_TEST(20, V20_Wacom_Bluetooth_Lift)
DEFINE_VECTOR_TEST(21, V21_Wacom_X_Coord_0xC0_Valid)
DEFINE_VECTOR_TEST(22, V22_Huion_ExpressKey_Reject_0xE0)
DEFINE_VECTOR_TEST(23, V23_Huion_TouchStrip_Reject_0xF0)
DEFINE_VECTOR_TEST(24, V24_VEIKK_ExpressKey_Reject_0x42)
DEFINE_VECTOR_TEST(25, V25_VEIKK_Pen_Lift_0x41_0xC0)
DEFINE_VECTOR_TEST(26, V26_XPPen_24bit_HighRes)
DEFINE_VECTOR_TEST(27, V27_Wacom_Pro_24bit_0x10)
DEFINE_VECTOR_TEST(28, V28_XPPen_Hover_0x80_NonZero)
DEFINE_VECTOR_TEST(29, V29_Huion_Pen_Lift_0x00_Reject)
DEFINE_VECTOR_TEST(30, V30_Artisul_ZeroFrame_Lift_Reject)
DEFINE_VECTOR_TEST(31, V31_VEIKK_Invalid_Status_0x00)

E2E_TEST(Decoder_TipPressureThresholdGating, TestTier::Tier1_Feature) {
    TabletSpec spec{};
    spec.vid = VendorID::Wacom;
    spec.report_id = 0x02;
    spec.x_offset = 2;
    spec.y_offset = 4;
    spec.tip_pressure_threshold = 20;

    // Report with pressure 10 (< threshold 20) -> tip NOT pressed
    uint8_t repBelow[10] = { 0x02, 0x81, 0x3C, 0x20, 0x18, 0x25, 0x0A, 0x00, 0x00, 0x00 };
    PenSample sampleBelow{};
    DecodeReason r1 = DecodeSample(repBelow, 10, spec, sampleBelow);
    ASSERT_EQ(r1, DecodeReason::Ok);
    ASSERT_EQ(sampleBelow.flags & 1, 0); // Not pressed

    // Report with pressure 25 (>= threshold 20) -> tip pressed
    uint8_t repAbove[10] = { 0x02, 0x81, 0x3C, 0x20, 0x18, 0x25, 0x19, 0x00, 0x00, 0x00 };
    PenSample sampleAbove{};
    DecodeReason r2 = DecodeSample(repAbove, 10, spec, sampleAbove);
    ASSERT_EQ(r2, DecodeReason::Ok);
    ASSERT_TRUE((sampleAbove.flags & 1) != 0); // Pressed
}

E2E_TEST(Decoder_MouseMode_StandardReport, TestTier::Tier1_Feature) {
    TabletSpec spec{};
    spec.is_mouse_mode = true;
    spec.report_id = 0x01;
    spec.x_offset = 2;
    spec.y_offset = 4;

    uint8_t report[6] = { 0x01, 0x03, 0xE8, 0x03, 0xD0, 0x07 }; // Left+Right buttons, x=1000, y=2000
    PenSample sample{};
    DecodeReason reason = DecodeSample(report, 6, spec, sample);

    ASSERT_EQ(reason, DecodeReason::Ok);
    ASSERT_EQ(sample.raw_x, 1000);
    ASSERT_EQ(sample.raw_y, 2000);
    ASSERT_TRUE((sample.flags & 1) != 0); // Left button pressed
    ASSERT_TRUE((sample.flags & 4) != 0); // Right / barrel button pressed
}

E2E_TEST(Decoder_UnknownVendor_GenericFallback, TestTier::Tier1_Feature) {
    TabletSpec spec{};
    spec.vid = 0x9999; // Unknown third party VID
    spec.report_id = 0x02;
    spec.x_offset = 2;
    spec.y_offset = 4;
    spec.coord_bits = 16;

    uint8_t report[8] = { 0x02, 0x81, 0x50, 0x00, 0x60, 0x00, 0x00, 0x04 };
    PenSample sample{};
    DecodeReason reason = DecodeSample(report, 8, spec, sample);

    ASSERT_EQ(reason, DecodeReason::Ok);
    ASSERT_EQ(sample.raw_x, 0x0050);
    ASSERT_EQ(sample.raw_y, 0x0060);
    ASSERT_EQ(sample.pressure, 0x0400);
}

// Decoder Tier 2 Boundary Cases

E2E_TEST(Decoder_ShortReport_Rejection, TestTier::Tier2_Boundary) {
    TabletSpec spec{};
    PenSample sample{};
    uint8_t shortRep[2] = { 0x02, 0x81 };
    ASSERT_EQ(DecodeSample(shortRep, 2, spec, sample), DecodeReason::ShortReport);
    ASSERT_EQ(DecodeSample(nullptr, 0, spec, sample), DecodeReason::ShortReport);
}

E2E_TEST(Decoder_WrongReportId_Rejection, TestTier::Tier2_Boundary) {
    TabletSpec spec{};
    spec.vid = VendorID::Wacom;
    spec.report_id = 0x02;
    PenSample sample{};
    uint8_t wrongIdRep[6] = { 0x07, 0x81, 0x00, 0x00, 0x00, 0x00 };
    ASSERT_EQ(DecodeSample(wrongIdRep, 6, spec, sample), DecodeReason::WrongReportId);
}

E2E_TEST(Decoder_Huion18Bit_PackedByte8_Decoding, TestTier::Tier2_Boundary) {
    TabletSpec spec{};
    spec.vid = VendorID::Huion;
    spec.report_id = 0x08;
    spec.x_offset = 2;
    spec.y_offset = 4;
    spec.coord_bits = 18;
    spec.max_x = 2000000;
    spec.max_y = 2000000;

    // Bit 0 & bit 2 of byte 8 set -> bits 16 and 17 of X set (+0x30000)
    // Bit 1 & bit 3 of byte 8 set -> bits 16 and 17 of Y set (+0x30000)
    uint8_t rep[10] = { 0x08, 0x81, 0x00, 0x10, 0x00, 0x20, 0x00, 0x00, 0x0F, 0x00 };
    PenSample sample{};
    DecodeReason r = DecodeSample(rep, 10, spec, sample);

    ASSERT_EQ(r, DecodeReason::Ok);
    ASSERT_EQ(sample.raw_x, 0x1000 | (3 << 16));
    ASSERT_EQ(sample.raw_y, 0x2000 | (3 << 16));
}

E2E_TEST(Decoder_ZeroFrameLift_Rejection, TestTier::Tier2_Boundary) {
    TabletSpec spec{};
    spec.vid = VendorID::Wacom;
    spec.report_id = 0x02;
    uint8_t liftRep[10] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    PenSample sample{};
    DecodeReason r = DecodeSample(liftRep, 10, spec, sample);
    ASSERT_EQ(r, DecodeReason::ZeroFrame);
}

// Decoder Tier 3 Cross-Feature Combination

E2E_TEST(Decoder_Decode_Then_Transform_EndToEnd, TestTier::Tier3_CrossFeature) {
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

    // Ingest V1: raw_x = 8252, raw_y = 9496
    const auto& v1 = g_test_vectors[0];
    PenSample sample{};
    DecodeReason r = DecodeSample(v1.data.data(), static_cast<int>(v1.data.size()), spec, sample);
    ASSERT_EQ(r, DecodeReason::Ok);

    int px = 0, py = 0;
    tf.TransformRawFast(sample.raw_x, sample.raw_y, px, py);

    ASSERT_GE(px, 0);
    ASSERT_LE(px, 1919);
    ASSERT_GE(py, 0);
    ASSERT_LE(py, 1079);
}

E2E_TEST(Decoder_Huion17Bit_Decode_Transform_Extrapolate, TestTier::Tier3_CrossFeature) {
    TabletSpec spec{};
    spec.vid = VendorID::Huion;
    spec.report_id = 0x08;
    spec.x_offset = 2;
    spec.y_offset = 4;
    spec.coord_bits = 17;
    spec.phys_w = 250.0;
    spec.phys_h = 150.0;
    spec.max_x = 0x1FFFF;
    spec.max_y = 0x1FFFF;

    CoordinateTransformer tf;
    tf.Configure(spec, 250.0, 150.0, 125.0, 75.0, 0.0, 0, 0, 2560, 1440, {0, 0, 2560, 1440}, {0, 0, 2560, 1440});

    KinematicExtrapolator ext;
    ExtrapolationConfig cfg{true, 2.5};

    const auto& v17 = g_test_vectors[16];
    PenSample s{};
    DecodeReason r = DecodeSample(v17.data.data(), static_cast<int>(v17.data.size()), spec, s);
    ASSERT_EQ(r, DecodeReason::Ok);
    ASSERT_EQ(s.raw_x, 0x17C20);
    ASSERT_EQ(s.raw_y, 0x14E50);

    int px, py;
    tf.TransformRawFast(s.raw_x, s.raw_y, px, py);
    double ox, oy;
    ext.UpdateWithDt(px, py, 4.0, cfg, ox, oy);

    ASSERT_FALSE(std::isnan(ox));
    ASSERT_FALSE(std::isnan(oy));
}

// --- CLI Runner & ANSI Reporting ---

void PrintUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --all         Run all tests across Tiers 1-4 (default)\n"
              << "  --tier1       Run Tier 1 (Feature Coverage) tests only\n"
              << "  --tier2       Run Tier 2 (Boundary & Corner Cases) tests only\n"
              << "  --tier3       Run Tier 3 (Cross-Feature Combinations) tests only\n"
              << "  --tier4       Run Tier 4 (Real-World Workloads) tests only\n"
              << "  --benchmark   Run 100,000-packet latency and jitter benchmark\n"
              << "  --help        Display this help message\n";
}

int main(int argc, char* argv[]) {
    bool runTier1 = true;
    bool runTier2 = true;
    bool runTier3 = true;
    bool runTier4 = true;
    bool benchmarkOnly = false;

    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return 0;
        } else if (arg == "--tier1") {
            runTier1 = true; runTier2 = false; runTier3 = false; runTier4 = false;
        } else if (arg == "--tier2") {
            runTier1 = false; runTier2 = true; runTier3 = false; runTier4 = false;
        } else if (arg == "--tier3") {
            runTier1 = false; runTier2 = false; runTier3 = true; runTier4 = false;
        } else if (arg == "--tier4") {
            runTier1 = false; runTier2 = false; runTier3 = false; runTier4 = true;
        } else if (arg == "--benchmark") {
            benchmarkOnly = true;
        } else if (arg == "--all") {
            runTier1 = runTier2 = runTier3 = runTier4 = true;
        }
    }

    std::cout << "\033[1;36m=======================================================================\033[0m\n";
    std::cout << "\033[1;36m     osuPoint Linux Port — Automated Synthetic Verification Test Suite \033[0m\n";
    std::cout << "\033[1;36m=======================================================================\033[0m\n\n";

    const auto& tests = TestRegistry::Instance().GetTests();

    size_t totalSelected = 0;
    size_t totalPassed = 0;
    size_t totalFailed = 0;

    size_t t1Pass = 0, t1Total = 0;
    size_t t2Pass = 0, t2Total = 0;
    size_t t3Pass = 0, t3Total = 0;
    size_t t4Pass = 0, t4Total = 0;

    auto startSuiteTime = std::chrono::steady_clock::now();

    for (const auto& test : tests) {
        bool shouldRun = false;
        if (benchmarkOnly) {
            shouldRun = (test.name.find("Benchmark") != std::string::npos);
        } else {
            if (test.tier == TestTier::Tier1_Feature && runTier1) shouldRun = true;
            if (test.tier == TestTier::Tier2_Boundary && runTier2) shouldRun = true;
            if (test.tier == TestTier::Tier3_CrossFeature && runTier3) shouldRun = true;
            if (test.tier == TestTier::Tier4_RealWorld && runTier4) shouldRun = true;
        }

        if (!shouldRun) continue;

        totalSelected++;
        if (test.tier == TestTier::Tier1_Feature) t1Total++;
        else if (test.tier == TestTier::Tier2_Boundary) t2Total++;
        else if (test.tier == TestTier::Tier3_CrossFeature) t3Total++;
        else if (test.tier == TestTier::Tier4_RealWorld) t4Total++;

        auto t0 = std::chrono::steady_clock::now();
        bool pass = true;
        std::string failMsg;

        try {
            test.func();
        } catch (const TestFailureException& ex) {
            pass = false;
            failMsg = ex.Detail();
        } catch (const std::exception& ex) {
            pass = false;
            failMsg = std::string("Unexpected exception: ") + ex.what();
        } catch (...) {
            pass = false;
            failMsg = "Unknown non-standard exception thrown";
        }
        auto t1 = std::chrono::steady_clock::now();
        auto durationUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

        if (pass) {
            totalPassed++;
            if (test.tier == TestTier::Tier1_Feature) t1Pass++;
            else if (test.tier == TestTier::Tier2_Boundary) t2Pass++;
            else if (test.tier == TestTier::Tier3_CrossFeature) t3Pass++;
            else if (test.tier == TestTier::Tier4_RealWorld) t4Pass++;

            std::cout << "  \033[32m[PASS]\033[0m [" << TierToShortString(test.tier) << "] "
                      << std::left << std::setw(55) << test.name
                      << " (" << std::right << std::setw(6) << durationUs << " us)\n";
        } else {
            totalFailed++;
            std::cout << "  \033[1;31m[FAIL]\033[0m [" << TierToShortString(test.tier) << "] "
                      << std::left << std::setw(55) << test.name << "\n";
            std::cout << "         \033[31mError: " << failMsg << "\033[0m\n";
        }
    }

    auto endSuiteTime = std::chrono::steady_clock::now();
    auto totalSuiteMs = std::chrono::duration_cast<std::chrono::milliseconds>(endSuiteTime - startSuiteTime).count();

    std::cout << "\n\033[1;36m-----------------------------------------------------------------------\033[0m\n";
    std::cout << "  Verification Summary by Tier:\n";
    if (t1Total > 0) {
        std::cout << "    - " << std::left << std::setw(38) << TierToString(TestTier::Tier1_Feature)
                  << ": " << t1Pass << " / " << t1Total << " passed\n";
    }
    if (t2Total > 0) {
        std::cout << "    - " << std::left << std::setw(38) << TierToString(TestTier::Tier2_Boundary)
                  << ": " << t2Pass << " / " << t2Total << " passed\n";
    }
    if (t3Total > 0) {
        std::cout << "    - " << std::left << std::setw(38) << TierToString(TestTier::Tier3_CrossFeature)
                  << ": " << t3Pass << " / " << t3Total << " passed\n";
    }
    if (t4Total > 0) {
        std::cout << "    - " << std::left << std::setw(38) << TierToString(TestTier::Tier4_RealWorld)
                  << ": " << t4Pass << " / " << t4Total << " passed\n";
    }
    std::cout << "-----------------------------------------------------------------------\n";
    std::cout << "  Total Execution Time: " << totalSuiteMs << " ms\n";
    std::cout << "  Total Tests Executed: " << totalSelected << "\n";
    std::cout << "  Passed:               \033[1;32m" << totalPassed << "\033[0m\n";
    std::cout << "  Failed:               " << (totalFailed > 0 ? "\033[1;31m" : "\033[1;32m") << totalFailed << "\033[0m\n";
    std::cout << "\033[1;36m=======================================================================\033[0m\n";

    if (totalFailed == 0) {
        std::cout << "\033[1;32mALL VERIFICATION TESTS PASSED SUCCESSFULLY (100% PASS RATE)\033[0m\n\n";
        return 0;
    } else {
        std::cout << "\033[1;31mSOME VERIFICATION TESTS FAILED\033[0m\n\n";
        return 1;
    }
}
