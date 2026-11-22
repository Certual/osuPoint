#include "test_framework.h"
#include "test_filter_impl.h"
#include <cmath>

// --- TIER 1: Feature Coverage ---

E2E_TEST(OneEuro_StationaryInput_SuppressesJitter, TestTier::Tier1_Feature) {
    OneEuroFilter filter(1.0, 0.007, 1.0);
    double base = 100.0;
    double dt = 0.004; // 250 Hz

    // Prime the filter
    for (int i = 0; i < 10; ++i) {
        filter.Filter(base, dt);
    }

    // Add high frequency jitter +/- 0.5 mm
    double maxDeviation = 0.0;
    for (int i = 0; i < 50; ++i) {
        double noise = ((i % 2 == 0) ? 0.5 : -0.5);
        double out = filter.Filter(base + noise, dt);
        double dev = std::abs(out - base);
        if (dev > maxDeviation) maxDeviation = dev;
    }

    // The filter should heavily attenuate high-frequency micro-jitter
    ASSERT_LE(maxDeviation, 0.25);
}

E2E_TEST(OneEuro_HighSpeedFlick_MinimizesTrackingLag, TestTier::Tier1_Feature) {
    OneEuroFilter filter(1.0, 0.05, 1.0);
    double dt = 0.004; // 250 Hz

    // High speed flick: moving at 500 mm/s (2.0 mm per 4ms frame)
    double pos = 0.0;
    double lastOut = 0.0;
    for (int i = 0; i < 30; ++i) {
        pos += 2.0;
        lastOut = filter.Filter(pos, dt);
    }

    // At high speeds, dynamic cutoff increases so lag is strictly bounded (< 7 ms at 500 mm/s)
    double lag = std::abs(pos - lastOut);
    ASSERT_LE(lag, 3.5);
}

E2E_TEST(OneEuro_FilterStrengthScaling, TestTier::Tier1_Feature) {
    OneEuroFilter2D fLow, fHigh;
    fLow.Configure(10);   // Light filtering
    fHigh.Configure(90);  // Heavy filtering

    ASSERT_TRUE(fLow.IsEnabled());
    ASSERT_TRUE(fHigh.IsEnabled());

    double dt = 0.004;
    double oxLow, oyLow, oxHigh, oyHigh;

    // Send a step input
    fLow.Filter(0.0, 0.0, dt, oxLow, oyLow);
    fHigh.Filter(0.0, 0.0, dt, oxHigh, oyHigh);

    fLow.Filter(100.0, 100.0, dt, oxLow, oyLow);
    fHigh.Filter(100.0, 100.0, dt, oxHigh, oyHigh);

    // Light filter should respond faster (higher output) than heavy filter
    ASSERT_GE(oxLow, oxHigh);
    ASSERT_GE(oyLow, oyHigh);
}

E2E_TEST(SoftKnee_RestingSpeed_MaximumSmoothing, TestTier::Tier1_Feature) {
    SoftKneeMotionFilter filter(40, 15.0, 65.0);
    double dt = 0.004;

    // Resting speed: 5.0 mm/s (< vStart = 15.0 mm/s)
    double s = SoftKneeMotionFilter::ComputeSmoothstep(5.0, 15.0, 65.0);
    ASSERT_EQ(s, 0.0);

    // Filter step response at low speed
    double ox = 0.0, oy = 0.0;
    filter.Filter(100.0, 100.0, dt, ox, oy);
    filter.Filter(100.02, 100.02, dt, ox, oy);

    // Output is smoothed, not identity
    ASSERT_NEAR(ox, 100.0, 0.03);
    ASSERT_NEAR(oy, 100.0, 0.03);
}

E2E_TEST(SoftKnee_HighSpeedFlick_Passthrough, TestTier::Tier1_Feature) {
    SoftKneeMotionFilter filter(40, 15.0, 65.0);
    double dt = 0.004;

    // Prime position
    double ox = 0.0, oy = 0.0;
    filter.Filter(0.0, 0.0, dt, ox, oy);

    // Flick at 100 mm/s (> vFull = 65.0 mm/s) -> 0.4 mm per frame
    double s = SoftKneeMotionFilter::ComputeSmoothstep(100.0, 15.0, 65.0);
    ASSERT_EQ(s, 1.0);

    filter.Filter(0.4, 0.0, dt, ox, oy);
    // At speed >= 65 mm/s, S(t) = 1.0 -> 100% raw passthrough
    ASSERT_NEAR(ox, 0.4, 1e-4);
    ASSERT_NEAR(oy, 0.0, 1e-4);
}

E2E_TEST(SoftKnee_MidpointTransition_HermiteBlending, TestTier::Tier1_Feature) {
    // At v = 40.0 mm/s, exactly halfway between 15.0 and 65.0 mm/s
    // t = (40 - 15) / (65 - 15) = 25 / 50 = 0.5
    // S(0.5) = 0.5^2 * (3 - 2*0.5) = 0.25 * 2.0 = 0.5
    double s = SoftKneeMotionFilter::ComputeSmoothstep(40.0, 15.0, 65.0);
    ASSERT_NEAR(s, 0.5, 1e-6);
}

// Extrapolator Scenarios 1-10 matching selftest.cpp:301-415

E2E_TEST(Extrapolator_Scenario1_DisabledModePassthrough, TestTier::Tier1_Feature) {
    KinematicExtrapolator ext;
    ExtrapolationConfig cfg;
    cfg.enabled = false;
    cfg.tau_ms = 2.5;
    ext.Reset();

    double ox = 0.0, oy = 0.0;
    ext.UpdateWithDt(100.0, 200.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(120.0, 240.0, 4.0, cfg, ox, oy);

    ASSERT_EQ(ox, 120.0);
    ASSERT_EQ(oy, 240.0);
    ASSERT_FALSE(ext.HasActiveLead());
}

E2E_TEST(Extrapolator_Scenario2_StationaryJitterDeadzoneSuppression, TestTier::Tier1_Feature) {
    KinematicExtrapolator ext;
    ExtrapolationConfig cfg;
    cfg.enabled = true;
    cfg.tau_ms = 2.5;
    ext.Reset();

    double ox = 0.0, oy = 0.0;
    ext.UpdateWithDt(500.0, 500.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(500.005, 500.002, 4.0, cfg, ox, oy);

    ASSERT_NEAR(ox, 500.005, 1e-6);
    ASSERT_NEAR(oy, 500.002, 1e-6);
    ASSERT_FALSE(ext.HasActiveLead());
}

E2E_TEST(Extrapolator_Scenario3_ConstantVelocityForwardPrediction, TestTier::Tier1_Feature) {
    KinematicExtrapolator ext;
    ExtrapolationConfig cfg;
    cfg.enabled = true;
    cfg.tau_ms = 2.5;
    ext.Reset();

    double ox = 0.0, oy = 0.0;
    ext.UpdateWithDt(0.0, 0.0, 4.0, cfg, ox, oy);    // p0
    ext.UpdateWithDt(20.0, 0.0, 4.0, cfg, ox, oy);   // p1: v=5.0
    ext.UpdateWithDt(40.0, 0.0, 4.0, cfg, ox, oy);   // p2: v=5.0, a=0.0 -> lead = 5.0 * 2.5 = 12.5 -> out = 52.5

    ASSERT_NEAR(ox, 52.5, 1e-3);
    ASSERT_NEAR(oy, 0.0, 1e-3);
    ASSERT_TRUE(ext.HasActiveLead());

    ext.UpdateWithDt(60.0, 0.0, 4.0, cfg, ox, oy);   // p3: v=5.0, a=0.0 -> out = 72.5
    ASSERT_NEAR(ox, 72.5, 1e-3);
    ASSERT_NEAR(oy, 0.0, 1e-3);
}

E2E_TEST(Extrapolator_Scenario4_TurnaroundSuppression, TestTier::Tier1_Feature) {
    KinematicExtrapolator ext;
    ExtrapolationConfig cfg;
    cfg.enabled = true;
    cfg.tau_ms = 2.5;
    ext.Reset();

    double ox = 0.0, oy = 0.0;
    ext.UpdateWithDt(0.0, 0.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(20.0, 0.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(40.0, 0.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(60.0, 0.0, 4.0, cfg, ox, oy);
    // Sharp reversal: from 60 to 55 (vx = -1.25, prev_vx = 5.0 -> dot < 0 -> lead = 0)
    ext.UpdateWithDt(55.0, 0.0, 4.0, cfg, ox, oy);

    ASSERT_EQ(ox, 55.0);
    ASSERT_EQ(oy, 0.0);
    ASSERT_FALSE(ext.HasActiveLead());
}

E2E_TEST(Extrapolator_Scenario5_TrueDiagonalTurnaroundSuppression, TestTier::Tier1_Feature) {
    KinematicExtrapolator ext;
    ExtrapolationConfig cfg;
    cfg.enabled = true;
    cfg.tau_ms = 2.5;
    ext.Reset();

    double ox = 0.0, oy = 0.0;
    ext.UpdateWithDt(0.0, 0.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(10.0, 10.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(20.0, 20.0, 4.0, cfg, ox, oy);  // v=(2.5, 2.5)
    // Diagonal reversal: dot = -1.25*2.5 + -1.25*2.5 = -6.25 < 0 -> lead = 0
    ext.UpdateWithDt(15.0, 15.0, 4.0, cfg, ox, oy);

    ASSERT_EQ(ox, 15.0);
    ASSERT_EQ(oy, 15.0);
    ASSERT_FALSE(ext.HasActiveLead());
}

E2E_TEST(Extrapolator_Scenario6_OrthogonalCornerMotion, TestTier::Tier1_Feature) {
    KinematicExtrapolator ext;
    ExtrapolationConfig cfg;
    cfg.enabled = true;
    cfg.tau_ms = 2.5;
    ext.Reset();

    double ox = 0.0, oy = 0.0;
    ext.UpdateWithDt(0.0, 0.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(20.0, 0.0, 4.0, cfg, ox, oy);   // v=(5.0, 0.0)
    ext.UpdateWithDt(40.0, 0.0, 4.0, cfg, ox, oy);   // v=(5.0, 0.0)
    // 90-degree orthogonal corner: v=(0.0, 5.0) -> dot = 0.0 (not turnaround)
    ext.UpdateWithDt(40.0, 20.0, 4.0, cfg, ox, oy);

    ASSERT_GE(oy, 20.0);
}

E2E_TEST(Extrapolator_Scenario7_AccelerationFromStandstill, TestTier::Tier1_Feature) {
    KinematicExtrapolator ext;
    ExtrapolationConfig cfg;
    cfg.enabled = true;
    cfg.tau_ms = 2.5;
    ext.Reset();

    double ox = 0.0, oy = 0.0;
    ext.UpdateWithDt(500.0, 500.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(500.002, 500.001, 4.0, cfg, ox, oy); // stationary deadband
    // Flick from rest starts:
    ext.UpdateWithDt(520.0, 500.001, 4.0, cfg, ox, oy);

    ASSERT_GE(ox, 520.0);
    ASSERT_TRUE(ext.HasActiveLead());
}

E2E_TEST(Extrapolator_Scenario8_SharpBrakingSuppression, TestTier::Tier1_Feature) {
    KinematicExtrapolator ext;
    ExtrapolationConfig cfg;
    cfg.enabled = true;
    cfg.tau_ms = 2.5;
    ext.Reset();

    double ox = 0.0, oy = 0.0;
    ext.UpdateWithDt(0.0, 0.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(40.0, 0.0, 4.0, cfg, ox, oy);   // v=10.0
    ext.UpdateWithDt(80.0, 0.0, 4.0, cfg, ox, oy);   // v=10.0
    // Velocity drops from 10.0 to 0.25 (97.5% drop -> sharp stop -> lead = 0)
    ext.UpdateWithDt(81.0, 0.0, 4.0, cfg, ox, oy);

    ASSERT_EQ(ox, 81.0);
    ASSERT_EQ(oy, 0.0);
    ASSERT_FALSE(ext.HasActiveLead());
}

E2E_TEST(Extrapolator_Scenario9_TauScalingLinearity, TestTier::Tier1_Feature) {
    KinematicExtrapolator ext;
    ExtrapolationConfig cfg;
    cfg.enabled = true;
    cfg.tau_ms = 1.0;
    ext.Reset();

    double ox1 = 0.0, oy1 = 0.0;
    ext.UpdateWithDt(0.0, 0.0, 4.0, cfg, ox1, oy1);
    ext.UpdateWithDt(20.0, 0.0, 4.0, cfg, ox1, oy1);
    ext.UpdateWithDt(40.0, 0.0, 4.0, cfg, ox1, oy1); // v=5.0, tau=1.0 -> out = 45.0
    ASSERT_NEAR(ox1, 45.0, 1e-3);

    cfg.tau_ms = 5.0;
    ext.Reset();
    double ox5 = 0.0, oy5 = 0.0;
    ext.UpdateWithDt(0.0, 0.0, 4.0, cfg, ox5, oy5);
    ext.UpdateWithDt(20.0, 0.0, 4.0, cfg, ox5, oy5);
    ext.UpdateWithDt(40.0, 0.0, 4.0, cfg, ox5, oy5); // v=5.0, tau=5.0 -> out = 65.0
    ASSERT_NEAR(ox5, 65.0, 1e-3);
}

E2E_TEST(Extrapolator_Scenario10_ResetClearsAllMomentum, TestTier::Tier1_Feature) {
    KinematicExtrapolator ext;
    ExtrapolationConfig cfg;
    cfg.enabled = true;
    cfg.tau_ms = 2.5;
    ext.Reset();

    double ox = 0.0, oy = 0.0;
    ext.UpdateWithDt(1000.0, 2000.0, 4.0, cfg, ox, oy);
    ASSERT_EQ(ox, 1000.0);
    ASSERT_EQ(oy, 2000.0);
    ASSERT_FALSE(ext.HasActiveLead());
}

// --- TIER 2: Boundary & Corner Cases ---

E2E_TEST(OneEuro_IrregularDt_HandledGracefully, TestTier::Tier2_Boundary) {
    OneEuroFilter filter(1.0, 0.007, 1.0);
    double out1 = filter.Filter(50.0, 0.0);    // Zero dt
    ASSERT_EQ(out1, 50.0);

    double out2 = filter.Filter(60.0, -0.001); // Negative dt
    ASSERT_EQ(out2, 60.0);

    double out3 = filter.Filter(70.0, 5.0);    // Massive time jump
    ASSERT_GE(out3, 60.0);
}

E2E_TEST(SoftKnee_CutoffFrequency_Beyond100Percent, TestTier::Tier2_Boundary) {
    SoftKneeMotionFilter f100(100);
    ASSERT_NEAR(f100.CutoffHz(), 70.0 - 56.0, 1e-4); // 14.0 Hz

    SoftKneeMotionFilter f200(200);
    // cutoff = max(14.0 / sqrt(2.0), 3.0) = 14.0 / 1.4142 = ~9.899 Hz
    ASSERT_NEAR(f200.CutoffHz(), 14.0 / std::sqrt(2.0), 1e-2);

    SoftKneeMotionFilter fExtreme(10000);
    ASSERT_GE(fExtreme.CutoffHz(), 3.0); // Hard floor at 3.0 Hz
}

E2E_TEST(SoftKnee_ZeroStrength_CompletePassthrough, TestTier::Tier2_Boundary) {
    SoftKneeMotionFilter f0(0);
    double ox, oy;
    f0.Filter(123.45, 678.90, 0.004, ox, oy);
    ASSERT_EQ(ox, 123.45);
    ASSERT_EQ(oy, 678.90);
}

E2E_TEST(Extrapolator_InvalidDtWindow_ResetsState, TestTier::Tier2_Boundary) {
    KinematicExtrapolator ext;
    ExtrapolationConfig cfg;
    cfg.enabled = true;
    cfg.tau_ms = 2.5;

    double ox = 0.0, oy = 0.0;
    ext.UpdateWithDt(0.0, 0.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(20.0, 0.0, 4.0, cfg, ox, oy);

    // Too small dt (< 0.1 ms)
    ext.UpdateWithDt(20.01, 0.0, 0.05, cfg, ox, oy);
    ASSERT_EQ(ox, 20.01);
    ASSERT_FALSE(ext.HasActiveLead());

    // Too large dt (> 40.0 ms)
    ext.UpdateWithDt(50.0, 0.0, 50.0, cfg, ox, oy);
    ASSERT_EQ(ox, 50.0);
    ASSERT_FALSE(ext.HasActiveLead());
}

E2E_TEST(Extrapolator_TauClamping_To1_5ms, TestTier::Tier2_Boundary) {
    KinematicExtrapolator extMin, extMax;
    ExtrapolationConfig cfgMin{true, 0.1}; // under 1.0 -> clamps to 1.0
    ExtrapolationConfig cfgMax{true, 10.0}; // over 5.0 -> clamps to 5.0

    double oxMin = 0.0, oyMin = 0.0, oxMax = 0.0, oyMax = 0.0;
    extMin.UpdateWithDt(0.0, 0.0, 4.0, cfgMin, oxMin, oyMin);
    extMin.UpdateWithDt(20.0, 0.0, 4.0, cfgMin, oxMin, oyMin);
    extMin.UpdateWithDt(40.0, 0.0, 4.0, cfgMin, oxMin, oyMin); // v=5.0, clamped tau=1.0 -> 45.0

    extMax.UpdateWithDt(0.0, 0.0, 4.0, cfgMax, oxMax, oyMax);
    extMax.UpdateWithDt(20.0, 0.0, 4.0, cfgMax, oxMax, oyMax);
    extMax.UpdateWithDt(40.0, 0.0, 4.0, cfgMax, oxMax, oyMax); // v=5.0, clamped tau=5.0 -> 65.0

    ASSERT_NEAR(oxMin, 45.0, 1e-3);
    ASSERT_NEAR(oxMax, 65.0, 1e-3);
}

E2E_TEST(Extrapolator_LeadMagnitudeCap_Enforced, TestTier::Tier2_Boundary) {
    KinematicExtrapolator ext;
    ExtrapolationConfig cfg{true, 5.0};

    double ox = 0.0, oy = 0.0;
    ext.UpdateWithDt(0.0, 0.0, 4.0, cfg, ox, oy);
    ext.UpdateWithDt(10.0, 0.0, 4.0, cfg, ox, oy); // v=2.5
    ext.UpdateWithDt(40.0, 0.0, 4.0, cfg, ox, oy); // v=7.5, massive acceleration ax = 5.0/4.0 = 1.25

    // Lead vector must not exceed maxLead = 1.5 * tau * speed = 1.5 * 5.0 * 7.5 = 56.25
    double leadX = ox - 40.0;
    ASSERT_LE(leadX, 56.25 + 1e-3);
}

// --- TIER 3: Cross-Feature Combinations ---

E2E_TEST(Filters_Cascade_OneEuro_SoftKnee_Extrapolator, TestTier::Tier3_CrossFeature) {
    OneEuroFilter2D fOneEuro;
    fOneEuro.Configure(25);

    SoftKneeMotionFilter fSoftKnee(40);

    KinematicExtrapolator fExtra;
    ExtrapolationConfig extraCfg{true, 2.5};

    double dt = 0.004;
    double inX = 0.0, inY = 0.0;

    for (int frame = 0; frame < 50; ++frame) {
        inX += 1.0;
        inY += 0.5;

        // Stage 1: 1-Euro
        double s1X, s1Y;
        fOneEuro.Filter(inX, inY, dt, s1X, s1Y);

        // Stage 2: Soft-Knee
        double s2X, s2Y;
        fSoftKnee.Filter(s1X, s1Y, dt, s2X, s2Y);

        // Stage 3: Extrapolator
        double s3X, s3Y;
        fExtra.UpdateWithDt(s2X, s2Y, dt * 1000.0, extraCfg, s3X, s3Y);

        // Verify all stages remain finite and forward-progressing
        ASSERT_FALSE(std::isnan(s3X));
        ASSERT_FALSE(std::isnan(s3Y));
        ASSERT_GE(s3X, 0.0);
        ASSERT_GE(s3Y, 0.0);
    }
}

E2E_TEST(Filters_HighSpeedFlick_TurnaroundCascade, TestTier::Tier3_CrossFeature) {
    SoftKneeMotionFilter fSoftKnee(40);
    KinematicExtrapolator fExtra;
    ExtrapolationConfig extraCfg{true, 2.5};

    double dt = 0.004;

    // Fast flick right
    double curX = 0.0;
    double outX = 0.0, outY = 0.0;
    for (int i = 0; i < 10; ++i) {
        curX += 5.0; // 1250 mm/s
        double skX, skY;
        fSoftKnee.Filter(curX, 0.0, dt, skX, skY);
        fExtra.UpdateWithDt(skX, skY, dt * 1000.0, extraCfg, outX, outY);
    }
    ASSERT_TRUE(fExtra.HasActiveLead());

    // Instant hard flick reversal
    curX -= 5.0;
    double skX, skY;
    fSoftKnee.Filter(curX, 0.0, dt, skX, skY);
    fExtra.UpdateWithDt(skX, skY, dt * 1000.0, extraCfg, outX, outY);

    // Extrapolator MUST have immediately suppressed lead
    ASSERT_FALSE(fExtra.HasActiveLead());
    ASSERT_EQ(outX, skX);
}
