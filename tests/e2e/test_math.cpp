#include "test_framework.h"
#include "test_pipeline.h"
#include <cmath>

// --- TIER 1: Feature Coverage ---

E2E_TEST(Math_Rot0_Origin_MapsToScreenOrigin, TestTier::Tier1_Feature) {
    TabletSpec spec{};
    spec.phys_w = 152.0;
    spec.phys_h = 95.0;
    spec.max_x = 15200;
    spec.max_y = 9500;

    CoordinateTransformer tf;
    tf.Configure(spec, 152.0, 95.0, 76.0, 47.5, 0.0, 0, 0, 1920, 1080, {0, 0, 1920, 1080}, {0, 0, 1920, 1080});

    double un = 0.0, vn = 0.0;
    int px = 0, py = 0;
    tf.TransformRaw(0, 0, un, vn, px, py);

    ASSERT_EQ(px, 0);
    ASSERT_EQ(py, 0);
    ASSERT_NEAR(un, 0.0, 1e-6);
    ASSERT_NEAR(vn, 0.0, 1e-6);
}

E2E_TEST(Math_Rot0_MaxCoords_MapsToScreenMax, TestTier::Tier1_Feature) {
    TabletSpec spec{};
    spec.phys_w = 152.0;
    spec.phys_h = 95.0;
    spec.max_x = 15200;
    spec.max_y = 9500;

    CoordinateTransformer tf;
    tf.Configure(spec, 152.0, 95.0, 76.0, 47.5, 0.0, 0, 0, 1920, 1080, {0, 0, 1920, 1080}, {0, 0, 1920, 1080});

    double un = 0.0, vn = 0.0;
    int px = 0, py = 0;
    tf.TransformRaw(15200, 9500, un, vn, px, py);

    ASSERT_EQ(px, 1919);
    ASSERT_EQ(py, 1079);
    ASSERT_NEAR(un, 1.0, 1e-6);
    ASSERT_NEAR(vn, 1.0, 1e-6);
}

E2E_TEST(Math_Rot0_Center_MapsToScreenCenter, TestTier::Tier1_Feature) {
    TabletSpec spec{};
    spec.phys_w = 152.0;
    spec.phys_h = 95.0;
    spec.max_x = 15200;
    spec.max_y = 9500;

    CoordinateTransformer tf;
    tf.Configure(spec, 152.0, 95.0, 76.0, 47.5, 0.0, 0, 0, 1920, 1080, {0, 0, 1920, 1080}, {0, 0, 1920, 1080});

    double un = 0.0, vn = 0.0;
    int px = 0, py = 0;
    tf.TransformRaw(7600, 4750, un, vn, px, py);

    ASSERT_EQ(px, 959);
    ASSERT_EQ(py, 539);
    ASSERT_NEAR(un, 0.5, 1e-6);
    ASSERT_NEAR(vn, 0.5, 1e-6);
}

E2E_TEST(Math_CenterInvariance_AcrossRotations, TestTier::Tier1_Feature) {
    TabletSpec spec{};
    spec.phys_w = 152.0;
    spec.phys_h = 95.0;
    spec.max_x = 15200;
    spec.max_y = 9500;

    const double testAngles[] = {0.0, 10.0, -10.0, 45.0, -45.0, 90.0, 180.0, 270.0};
    for (double rot : testAngles) {
        CoordinateTransformer tf;
        tf.Configure(spec, 152.0, 95.0, 76.0, 47.5, rot, 0, 0, 1920, 1080, {0, 0, 1920, 1080}, {0, 0, 1920, 1080});

        double un = 0.0, vn = 0.0;
        int px = 0, py = 0;
        tf.TransformRaw(7600, 4750, un, vn, px, py);

        ASSERT_NEAR(un, 0.5, 1e-5);
        ASSERT_NEAR(vn, 0.5, 1e-5);
        ASSERT_EQ(px, 959);
        ASSERT_EQ(py, 539);
    }
}

E2E_TEST(Math_RotPlus10_AnalyticalTrigonometricValidation, TestTier::Tier1_Feature) {
    TabletSpec spec{};
    spec.phys_w = 152.0;
    spec.phys_h = 95.0;
    spec.max_x = 15200;
    spec.max_y = 9500;

    CoordinateTransformer tf;
    tf.Configure(spec, 152.0, 95.0, 76.0, 47.5, 10.0, 0, 0, 1920, 1080, {0, 0, 1920, 1080}, {0, 0, 1920, 1080});

    double un = 0.0, vn = 0.0;
    int px = 0, py = 0;
    tf.TransformRaw(15200, 4750, un, vn, px, py);

    const double rad = 10.0 * PI / 180.0;
    double expected_un = 0.5 + std::cos(rad) / 2.0;
    double expected_vn = 0.5 - std::sin(rad) * (76.0 / 95.0);

    ASSERT_NEAR(un, expected_un, 1e-4);
    ASSERT_NEAR(vn, expected_vn, 1e-4);
}

E2E_TEST(Math_RotMinus10_AnalyticalTrigonometricValidation, TestTier::Tier1_Feature) {
    TabletSpec spec{};
    spec.phys_w = 152.0;
    spec.phys_h = 95.0;
    spec.max_x = 15200;
    spec.max_y = 9500;

    CoordinateTransformer tf;
    tf.Configure(spec, 152.0, 95.0, 76.0, 47.5, -10.0, 0, 0, 1920, 1080, {0, 0, 1920, 1080}, {0, 0, 1920, 1080});

    double un = 0.0, vn = 0.0;
    int px = 0, py = 0;
    tf.TransformRaw(15200, 4750, un, vn, px, py);

    const double rad = -10.0 * PI / 180.0;
    double expected_un = 0.5 + std::cos(rad) / 2.0;
    double expected_vn = 0.5 - std::sin(rad) * (76.0 / 95.0);

    ASSERT_NEAR(un, expected_un, 1e-4);
    ASSERT_NEAR(vn, expected_vn, 1e-4);
}

E2E_TEST(Math_FastFusedMatrix_MatchesTwoStageTransform, TestTier::Tier1_Feature) {
    TabletSpec spec{};
    spec.phys_w = 152.0;
    spec.phys_h = 95.0;
    spec.max_x = 15200;
    spec.max_y = 9500;

    CoordinateTransformer tf;
    tf.Configure(spec, 100.0, 60.0, 60.0, 40.0, 25.0, 0, 0, 1920, 1080, {0, 0, 1920, 1080}, {0, 0, 1920, 1080});

    for (int rx = 1000; rx <= 15000; rx += 2000) {
        for (int ry = 1000; ry <= 9000; ry += 2000) {
            double un = 0.0, vn = 0.0;
            int px_norm = 0, py_norm = 0;
            int px_fast = 0, py_fast = 0;

            tf.TransformRaw(rx, ry, un, vn, px_norm, py_norm);
            tf.TransformRawFast(rx, ry, px_fast, py_fast);

            ASSERT_EQ(px_norm, px_fast);
            ASSERT_EQ(py_norm, py_fast);
        }
    }
}

E2E_TEST(Math_AspectRatio_ParsingStandardStrings, TestTier::Tier1_Feature) {
    ASSERT_NEAR(ParseAspectRatio("16:9"), 16.0 / 9.0, 1e-4);
    ASSERT_NEAR(ParseAspectRatio("16:10"), 16.0 / 10.0, 1e-4);
    ASSERT_NEAR(ParseAspectRatio("4:3"), 4.0 / 3.0, 1e-4);
    ASSERT_NEAR(ParseAspectRatio("21:9"), 21.0 / 9.0, 1e-4);
    ASSERT_NEAR(ParseAspectRatio("1:1"), 1.0, 1e-4);
    ASSERT_NEAR(ParseAspectRatio("3:2"), 1.5, 1e-4);
    ASSERT_NEAR(ParseAspectRatio("1.77777778"), 16.0 / 9.0, 1e-4);
}

E2E_TEST(Math_AspectRatio_LockFormulaVerification, TestTier::Tier1_Feature) {
    double ratio = ParseAspectRatio("16:9");
    double w = 152.0;
    double h = w / ratio;
    ASSERT_NEAR(h, 85.5, 1e-4);

    double back_w = h * ratio;
    ASSERT_NEAR(back_w, 152.0, 1e-4);
}

E2E_TEST(Math_DisplayRect_CenteringOffset, TestTier::Tier1_Feature) {
    TabletSpec spec{};
    spec.phys_w = 152.0;
    spec.phys_h = 95.0;
    spec.max_x = 15200;
    spec.max_y = 9500;

    CoordinateTransformer tf;
    // disp_x = -1, disp_y = -1 -> Auto-center 1280x720 inside 1920x1080
    // Expected target_l = (1920 - 1280) / 2 = 320, target_t = (1080 - 720) / 2 = 180
    tf.Configure(spec, 152.0, 95.0, 76.0, 47.5, 0.0, -1, -1, 1280, 720, {0, 0, 1920, 1080}, {0, 0, 1920, 1080});

    double un = 0.0, vn = 0.0;
    int px = 0, py = 0;
    tf.TransformRaw(0, 0, un, vn, px, py);
    ASSERT_EQ(px, 320);
    ASSERT_EQ(py, 180);

    tf.TransformRaw(15200, 9500, un, vn, px, py);
    ASSERT_EQ(px, 320 + 1280 - 1);
    ASSERT_EQ(py, 180 + 720 - 1);
}

E2E_TEST(Math_DisplayRect_ClippingEnforcement, TestTier::Tier1_Feature) {
    TabletSpec spec{};
    spec.phys_w = 152.0;
    spec.phys_h = 95.0;
    spec.max_x = 15200;
    spec.max_y = 9500;

    CoordinateTransformer tf_clipped;
    tf_clipped.Configure(spec, 50.0, 50.0, 76.0, 47.5, 0.0, 0, 0, 1920, 1080, {0, 0, 1920, 1080}, {0, 0, 1920, 1080}, true);

    double un = 0.0, vn = 0.0;
    int px = 0, py = 0;
    // Far outside active area
    tf_clipped.TransformRaw(0, 0, un, vn, px, py);
    ASSERT_GE(px, 0);
    ASSERT_GE(py, 0);
    ASSERT_LE(px, 1919);
    ASSERT_LE(py, 1079);

    tf_clipped.TransformRaw(15200, 9500, un, vn, px, py);
    ASSERT_GE(px, 0);
    ASSERT_GE(py, 0);
    ASSERT_LE(px, 1919);
    ASSERT_LE(py, 1079);
}

E2E_TEST(Math_LatencyStats_PercentileComputation, TestTier::Tier1_Feature) {
    LatencyStats stats;
    stats.Record(10);
    stats.Record(20);
    stats.Record(30);
    stats.Record(40);
    stats.Record(50);

    uint32_t p50 = 0, p99 = 0, maxUs = 0, minUs = 0;
    stats.Compute(p50, p99, maxUs, minUs);

    ASSERT_EQ(p50, 30u);
    ASSERT_EQ(minUs, 10u);
    ASSERT_EQ(maxUs, 50u);
    ASSERT_EQ(p99, 50u);
}

E2E_TEST(Math_JitterStats_StdDevAndAverageComputation, TestTier::Tier1_Feature) {
    JitterStats stats;
    stats.RecordInterval(7500);
    stats.RecordInterval(7520);
    stats.RecordInterval(7480);
    stats.RecordInterval(7510);

    uint32_t stdDev = 0, avgIntv = 0;
    stats.Compute(stdDev, avgIntv);

    ASSERT_GE(avgIntv, 7490u);
    ASSERT_LE(avgIntv, 7515u);
    ASSERT_GE(stdDev, 10u);
    ASSERT_LE(stdDev, 30u);
}

// --- TIER 2: Boundary & Corner Cases ---

E2E_TEST(Math_RotPlus45_AnalyticalTrigonometricValidation, TestTier::Tier2_Boundary) {
    TabletSpec spec{};
    spec.phys_w = 100.0;
    spec.phys_h = 100.0;
    spec.max_x = 10000;
    spec.max_y = 10000;

    CoordinateTransformer tf;
    tf.Configure(spec, 100.0, 100.0, 50.0, 50.0, 45.0, 0, 0, 1000, 1000, {0, 0, 1000, 1000}, {0, 0, 1000, 1000});

    double un = 0.0, vn = 0.0;
    int px = 0, py = 0;
    tf.TransformRaw(10000, 5000, un, vn, px, py);

    const double rad = 45.0 * PI / 180.0;
    double expected_un = 0.5 + std::cos(rad) * 0.5;
    double expected_vn = 0.5 - std::sin(rad) * 0.5;

    ASSERT_NEAR(un, expected_un, 1e-4);
    ASSERT_NEAR(vn, expected_vn, 1e-4);
}

E2E_TEST(Math_RotMinus45_AnalyticalTrigonometricValidation, TestTier::Tier2_Boundary) {
    TabletSpec spec{};
    spec.phys_w = 100.0;
    spec.phys_h = 100.0;
    spec.max_x = 10000;
    spec.max_y = 10000;

    CoordinateTransformer tf;
    tf.Configure(spec, 100.0, 100.0, 50.0, 50.0, -45.0, 0, 0, 1000, 1000, {0, 0, 1000, 1000}, {0, 0, 1000, 1000});

    double un = 0.0, vn = 0.0;
    int px = 0, py = 0;
    tf.TransformRaw(10000, 5000, un, vn, px, py);

    const double rad = -45.0 * PI / 180.0;
    double expected_un = 0.5 + std::cos(rad) * 0.5;
    double expected_vn = 0.5 - std::sin(rad) * 0.5;

    ASSERT_NEAR(un, expected_un, 1e-4);
    ASSERT_NEAR(vn, expected_vn, 1e-4);
}

E2E_TEST(Math_RotationPeriodicity_360DegreeInvariance, TestTier::Tier2_Boundary) {
    TabletSpec spec{};
    spec.phys_w = 152.0;
    spec.phys_h = 95.0;
    spec.max_x = 15200;
    spec.max_y = 9500;

    CoordinateTransformer tf0, tf360, tf720, tfMinus360;
    tf0.Configure(spec, 152.0, 95.0, 76.0, 47.5, 0.0, 0, 0, 1920, 1080, {0, 0, 1920, 1080}, {0, 0, 1920, 1080});
    tf360.Configure(spec, 152.0, 95.0, 76.0, 47.5, 360.0, 0, 0, 1920, 1080, {0, 0, 1920, 1080}, {0, 0, 1920, 1080});
    tf720.Configure(spec, 152.0, 95.0, 76.0, 47.5, 720.0, 0, 0, 1920, 1080, {0, 0, 1920, 1080}, {0, 0, 1920, 1080});
    tfMinus360.Configure(spec, 152.0, 95.0, 76.0, 47.5, -360.0, 0, 0, 1920, 1080, {0, 0, 1920, 1080}, {0, 0, 1920, 1080});

    int px0, py0, px360, py360, px720, py720, pxM360, pyM360;
    tf0.TransformRawFast(3000, 2000, px0, py0);
    tf360.TransformRawFast(3000, 2000, px360, py360);
    tf720.TransformRawFast(3000, 2000, px720, py720);
    tfMinus360.TransformRawFast(3000, 2000, pxM360, pyM360);

    ASSERT_EQ(px0, px360);
    ASSERT_EQ(py0, py360);
    ASSERT_EQ(px0, px720);
    ASSERT_EQ(py0, py720);
    ASSERT_EQ(px0, pxM360);
    ASSERT_EQ(py0, pyM360);
}

E2E_TEST(Math_CardinalRotations_90_180_270, TestTier::Tier2_Boundary) {
    TabletSpec spec{};
    spec.phys_w = 100.0;
    spec.phys_h = 100.0;
    spec.max_x = 10000;
    spec.max_y = 10000;

    CoordinateTransformer tf90, tf180, tf270;
    tf90.Configure(spec, 100.0, 100.0, 50.0, 50.0, 90.0, 0, 0, 1000, 1000, {0, 0, 1000, 1000}, {0, 0, 1000, 1000});
    tf180.Configure(spec, 100.0, 100.0, 50.0, 50.0, 180.0, 0, 0, 1000, 1000, {0, 0, 1000, 1000}, {0, 0, 1000, 1000});
    tf270.Configure(spec, 100.0, 100.0, 50.0, 50.0, 270.0, 0, 0, 1000, 1000, {0, 0, 1000, 1000}, {0, 0, 1000, 1000});

    double un, vn;
    int px, py;

    // Center invariant for all cardinal rotations
    tf90.TransformRaw(5000, 5000, un, vn, px, py);
    ASSERT_NEAR(un, 0.5, 1e-4);
    ASSERT_NEAR(vn, 0.5, 1e-4);
    ASSERT_EQ(px, 499);
    ASSERT_EQ(py, 499);

    tf180.TransformRaw(5000, 5000, un, vn, px, py);
    ASSERT_NEAR(un, 0.5, 1e-4);
    ASSERT_NEAR(vn, 0.5, 1e-4);
    ASSERT_EQ(px, 499);
    ASSERT_EQ(py, 499);

    tf270.TransformRaw(5000, 5000, un, vn, px, py);
    ASSERT_NEAR(un, 0.5, 1e-4);
    ASSERT_NEAR(vn, 0.5, 1e-4);
    ASSERT_EQ(px, 499);
    ASSERT_EQ(py, 499);

    // In rot=90: (10000, 10000) maps to un=1.0, vn=0.0
    tf90.TransformRaw(10000, 10000, un, vn, px, py);
    ASSERT_NEAR(un, 1.0, 1e-4);
    ASSERT_NEAR(vn, 0.0, 1e-4);

    // In rot=180: bottom-right (10000, 10000) maps to un=0.0, vn=0.0
    tf180.TransformRaw(10000, 10000, un, vn, px, py);
    ASSERT_NEAR(un, 0.0, 1e-4);
    ASSERT_NEAR(vn, 0.0, 1e-4);
    ASSERT_EQ(px, 0);
    ASSERT_EQ(py, 0);
}

E2E_TEST(Math_MicroActiveArea_PrecisionStability, TestTier::Tier2_Boundary) {
    TabletSpec spec{};
    spec.phys_w = 152.0;
    spec.phys_h = 95.0;
    spec.max_x = 15200;
    spec.max_y = 9500;

    CoordinateTransformer tf;
    // Micro area: 5x5 mm
    tf.Configure(spec, 5.0, 5.0, 76.0, 47.5, 0.0, 0, 0, 1920, 1080, {0, 0, 1920, 1080}, {0, 0, 1920, 1080});

    double un, vn;
    int px, py;
    tf.TransformRaw(7600, 4750, un, vn, px, py);
    ASSERT_EQ(px, 959);
    ASSERT_EQ(py, 539);
}

E2E_TEST(Math_GiantActiveArea_PrecisionStability, TestTier::Tier2_Boundary) {
    TabletSpec spec{};
    spec.phys_w = 400.0;
    spec.phys_h = 300.0;
    spec.max_x = 40000;
    spec.max_y = 30000;

    CoordinateTransformer tf;
    tf.Configure(spec, 400.0, 300.0, 200.0, 150.0, 0.0, 0, 0, 3840, 2160, {0, 0, 3840, 2160}, {0, 0, 3840, 2160});

    double un, vn;
    int px, py;
    tf.TransformRaw(40000, 30000, un, vn, px, py);
    ASSERT_EQ(px, 3839);
    ASSERT_EQ(py, 2159);
}

E2E_TEST(Math_AspectRatio_AlternativeDelimiters, TestTier::Tier2_Boundary) {
    ASSERT_NEAR(ParseAspectRatio("16/9"), 16.0 / 9.0, 1e-4);
    ASSERT_NEAR(ParseAspectRatio("16x9"), 16.0 / 9.0, 1e-4);
    ASSERT_NEAR(ParseAspectRatio("16X9"), 16.0 / 9.0, 1e-4);
    ASSERT_NEAR(ParseAspectRatio("  16:9  "), 16.0 / 9.0, 1e-4);
    ASSERT_NEAR(ParseAspectRatio("16, 9"), 16.0 / 9.0, 1e-4);
    ASSERT_NEAR(ParseAspectRatio("invalid_string"), 16.0 / 9.0, 1e-4);
    ASSERT_NEAR(ParseAspectRatio(""), 16.0 / 9.0, 1e-4);
}

E2E_TEST(Math_HighRes24BitCoordinates_NoOverflow, TestTier::Tier2_Boundary) {
    TabletSpec spec{};
    spec.phys_w = 200.0;
    spec.phys_h = 150.0;
    spec.max_x = 16777215; // 24-bit max
    spec.max_y = 16777215;

    CoordinateTransformer tf;
    tf.Configure(spec, 200.0, 150.0, 100.0, 75.0, 0.0, 0, 0, 3840, 2160, {0, 0, 3840, 2160}, {0, 0, 3840, 2160});

    double un, vn;
    int px, py;
    tf.TransformRaw(16777215, 16777215, un, vn, px, py);
    ASSERT_EQ(px, 3839);
    ASSERT_EQ(py, 2159);
}

E2E_TEST(Math_LatencyStats_SingleSampleAndUniformSamples, TestTier::Tier2_Boundary) {
    LatencyStats single;
    single.Record(42);
    uint32_t p50 = 0, p99 = 0, maxUs = 0, minUs = 0;
    single.Compute(p50, p99, maxUs, minUs);
    ASSERT_EQ(p50, 42u);
    ASSERT_EQ(p99, 42u);
    ASSERT_EQ(minUs, 42u);
    ASSERT_EQ(maxUs, 42u);

    LatencyStats uniform;
    for (int i = 0; i < 100; ++i) uniform.Record(25);
    uniform.Compute(p50, p99, maxUs, minUs);
    ASSERT_EQ(p50, 25u);
    ASSERT_EQ(p99, 25u);
    ASSERT_EQ(minUs, 25u);
    ASSERT_EQ(maxUs, 25u);
}

E2E_TEST(Math_JitterStats_ZeroVarianceClock, TestTier::Tier2_Boundary) {
    JitterStats stats;
    for (int i = 0; i < 50; ++i) stats.RecordInterval(1000); // Perfect 1000 Hz
    uint32_t stdDev = 0, avgIntv = 0;
    stats.Compute(stdDev, avgIntv);
    ASSERT_EQ(stdDev, 0u);
    ASSERT_EQ(avgIntv, 1000u);
}

// --- TIER 3: Cross-Feature Combinations ---

E2E_TEST(Math_Rotation45_CombinedWith_AspectRatioLock16_10, TestTier::Tier3_CrossFeature) {
    TabletSpec spec{};
    spec.phys_w = 160.0;
    spec.phys_h = 100.0;
    spec.max_x = 16000;
    spec.max_y = 10000;

    double ratio = ParseAspectRatio("16:10");
    double w = 120.0;
    double h = w / ratio; // 75.0 mm
    ASSERT_NEAR(h, 75.0, 1e-4);

    CoordinateTransformer tf;
    tf.Configure(spec, w, h, 80.0, 50.0, 45.0, 0, 0, 1920, 1200, {0, 0, 1920, 1200}, {0, 0, 1920, 1200});

    double un, vn;
    int px, py;
    tf.TransformRaw(8000, 5000, un, vn, px, py);
    ASSERT_NEAR(un, 0.5, 1e-4);
    ASSERT_NEAR(vn, 0.5, 1e-4);
    ASSERT_EQ(px, 1920 / 2 - 1);
    ASSERT_EQ(py, 1200 / 2 - 1);
}

E2E_TEST(Math_AsymmetricArea_CombinedWith_RotationMinus15, TestTier::Tier3_CrossFeature) {
    TabletSpec spec{};
    spec.phys_w = 152.0;
    spec.phys_h = 95.0;
    spec.max_x = 15200;
    spec.max_y = 9500;

    // Asymmetric active area placed toward upper right
    CoordinateTransformer tf;
    tf.Configure(spec, 80.0, 50.0, 100.0, 35.0, -15.0, 0, 0, 1920, 1080, {0, 0, 1920, 1080}, {0, 0, 1920, 1080});

    double un, vn;
    int px, py;
    // Center of this area: raw_x = 100.0 / 152.0 * 15200 = 10000, raw_y = 35.0 / 95.0 * 9500 = 3500
    tf.TransformRaw(10000, 3500, un, vn, px, py);
    ASSERT_NEAR(un, 0.5, 1e-4);
    ASSERT_NEAR(vn, 0.5, 1e-4);
    ASSERT_EQ(px, 959);
    ASSERT_EQ(py, 539);
}

E2E_TEST(Math_DisplayClipping_CombinedWith_CustomOffsetWindow, TestTier::Tier3_CrossFeature) {
    TabletSpec spec{};
    spec.phys_w = 152.0;
    spec.phys_h = 95.0;
    spec.max_x = 15200;
    spec.max_y = 9500;

    CoordinateTransformer tf;
    // Custom 1280x720 window offset at (100, 50) with clip_area=true
    tf.Configure(spec, 50.0, 50.0, 76.0, 47.5, 0.0, 100, 50, 1280, 720, {0, 0, 1920, 1080}, {0, 0, 1920, 1080}, true);

    double un, vn;
    int px, py;
    tf.TransformRaw(0, 0, un, vn, px, py);
    ASSERT_GE(px, 0);
    ASSERT_GE(py, 0);
    ASSERT_LE(px, 1919);
    ASSERT_LE(py, 1079);
}

E2E_TEST(Math_MultiMonitorTarget_MappingToMonitor2, TestTier::Tier3_CrossFeature) {
    TabletSpec spec{};
    spec.phys_w = 152.0;
    spec.phys_h = 95.0;
    spec.max_x = 15200;
    spec.max_y = 9500;

    // Dual monitor setup: Virtual desktop = 3840x1080. Monitor 2 is [1920, 0, 3840, 1080]
    CoordinateTransformer tf;
    tf.Configure(spec, 152.0, 95.0, 76.0, 47.5, 0.0, 0, 0, 1920, 1080, {1920, 0, 3840, 1080}, {0, 0, 3840, 1080});

    double un, vn;
    int px, py;
    tf.TransformRaw(0, 0, un, vn, px, py);
    ASSERT_EQ(px, 1920);
    ASSERT_EQ(py, 0);

    tf.TransformRaw(15200, 9500, un, vn, px, py);
    ASSERT_EQ(px, 3839);
    ASSERT_EQ(py, 1079);

    tf.TransformRaw(7600, 4750, un, vn, px, py);
    ASSERT_EQ(px, 1920 + 959);
    ASSERT_EQ(py, 539);
}
