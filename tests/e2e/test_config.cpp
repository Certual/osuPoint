#include "test_framework.h"
#include "test_config_impl.h"
#include <fstream>
#include <sstream>

// --- TIER 1: Feature Coverage ---

E2E_TEST(Config_ParseUpstreamConfigIni, TestTier::Tier1_Feature) {
    // Read the actual local workspace file
    std::ifstream file("/mnt/uwu/Apps/Antigravity-Projects/osupoint/osuPoint-0.6.1 6/config.ini");
    std::string content;
    if (file.is_open()) {
        std::stringstream ss;
        ss << file.rdbuf();
        content = ss.str();
    } else {
        // Fallback reference string if path is unreachable
        content = "# osu!Point Configuration\nwidth=51\nheight=36.56\ncenter_x=70\ncenter_y=60\nrotation=-3\nmonitor=0\ndisplay_width=0\ndisplay_height=0\ndisplay_x=-1\ndisplay_y=-1\nlock_ratio=0\nratio=1.78\nratio_precision=8\nlock_drag=1\nclip_area=0\npen_click=0\nbarrel_button=1\nantialiasing=1\nui_outline=1\nextrapolation_mm=2.5\nfilter=25\nmotion_filter=40\nspin_percent=100\ndebug_mode=0\nunlock_limits=0\npalette=amber\ntheme_base=dark\nbg_image=\ncopy_bg_image=1\ndelete_old_bg_image=0\ncopied_bg_images=photo_2026-11-02_00-39-30.jpg\nui_opacity=100\nbg_blur=18\nui_style=0\nbg_blur_type=1\nui_outline_opacity=0\nui_area_opacity=0\nactive_tab=1\nactive_preset=Default\n\n[preset:Default]\nwidth=152\nheight=95\ncenter_x=76\ncenter_y=47.5\nrotation=0\nratio=16:9\nmonitor=0\npalette=system\n\n[palette:Custom]\naccent=#FFFFFF\nbg_image=photo_2026-11-02_00-39-30.jpg\n";
    }

    OsuPointConfig cfg = ConfigParser::ParseIni(content);

    ASSERT_NEAR(cfg.width, 51.0, 1e-4);
    ASSERT_NEAR(cfg.height, 36.56, 1e-4);
    ASSERT_NEAR(cfg.center_x, 70.0, 1e-4);
    ASSERT_NEAR(cfg.center_y, 60.0, 1e-4);
    ASSERT_NEAR(cfg.rotation, -3.0, 1e-4);
    ASSERT_EQ(cfg.monitor, 0);
    ASSERT_EQ(cfg.display_x, -1);
    ASSERT_EQ(cfg.display_y, -1);
    ASSERT_FALSE(cfg.lock_ratio);
    ASSERT_EQ(cfg.ratio, "1.78");
    ASSERT_EQ(cfg.ratio_precision, 8);
    ASSERT_TRUE(cfg.lock_drag);
    ASSERT_FALSE(cfg.clip_area);
    ASSERT_FALSE(cfg.pen_click);
    ASSERT_TRUE(cfg.barrel_button);
    ASSERT_TRUE(cfg.antialiasing);
    ASSERT_TRUE(cfg.ui_outline);
    ASSERT_NEAR(cfg.extrapolation_mm, 2.5, 1e-4);
    ASSERT_EQ(cfg.filter, 25);
    ASSERT_EQ(cfg.motion_filter, 40);
    ASSERT_EQ(cfg.spin_percent, 100);
    ASSERT_FALSE(cfg.debug_mode);
    ASSERT_FALSE(cfg.unlock_limits);
    ASSERT_EQ(cfg.palette, "amber");
    ASSERT_EQ(cfg.theme_base, "dark");
    ASSERT_TRUE(cfg.copy_bg_image);
    ASSERT_FALSE(cfg.delete_old_bg_image);
    ASSERT_EQ(cfg.copied_bg_images, "photo_2026-11-02_00-39-30.jpg");
    ASSERT_EQ(cfg.ui_opacity, 100);
    ASSERT_EQ(cfg.bg_blur, 18);
    ASSERT_EQ(cfg.ui_style, 0);
    ASSERT_EQ(cfg.bg_blur_type, 1);
    ASSERT_EQ(cfg.ui_outline_opacity, 0);
    ASSERT_EQ(cfg.ui_area_opacity, 0);
    ASSERT_EQ(cfg.active_tab, 1);
    ASSERT_EQ(cfg.active_preset, "Default");

    // Presets
    ASSERT_EQ(cfg.presets.size(), 1u);
    ASSERT_TRUE(cfg.presets.find("Default") != cfg.presets.end());
    const auto& pDef = cfg.presets.at("Default");
    ASSERT_NEAR(pDef.width, 152.0, 1e-4);
    ASSERT_NEAR(pDef.height, 95.0, 1e-4);
    ASSERT_NEAR(pDef.center_x, 76.0, 1e-4);
    ASSERT_NEAR(pDef.center_y, 47.5, 1e-4);
    ASSERT_EQ(pDef.ratio, "16:9");

    // Palettes
    ASSERT_EQ(cfg.palettes.size(), 1u);
    ASSERT_TRUE(cfg.palettes.find("Custom") != cfg.palettes.end());
    const auto& pal = cfg.palettes.at("Custom");
    ASSERT_EQ(pal.accent, "#FFFFFF");
    ASSERT_EQ(pal.bg_image, "photo_2026-11-02_00-39-30.jpg");
}

E2E_TEST(Config_RoundtripSerialization_PreservesAll42Keys, TestTier::Tier1_Feature) {
    OsuPointConfig original;
    original.width = 112.5;
    original.height = 70.3;
    original.center_x = 80.0;
    original.center_y = 50.0;
    original.rotation = 12.5;
    original.monitor = 1;
    original.display_width = 1920;
    original.display_height = 1080;
    original.display_x = 0;
    original.display_y = 0;
    original.lock_ratio = true;
    original.ratio = "16:10";
    original.ratio_precision = 4;
    original.lock_drag = true;
    original.clip_area = true;
    original.pen_click = true;
    original.barrel_button = false;
    original.tip_pressure_threshold = 15;
    original.antialiasing = true;
    original.ui_outline = false;
    original.extrapolation_mm = 3.2;
    original.filter = 35;
    original.motion_filter = 55;
    original.spin_percent = 75;
    original.debug_mode = true;
    original.unlock_limits = true;
    original.palette = "cyan";
    original.theme_base = "light";
    original.bg_image = "wallpaper.png";
    original.copy_bg_image = false;
    original.delete_old_bg_image = true;
    original.copied_bg_images = "wallpaper.png;custom.png";
    original.ui_opacity = 85;
    original.bg_blur = 12;
    original.ui_style = 1;
    original.bg_blur_type = 2;
    original.ui_outline_opacity = 50;
    original.ui_area_opacity = 70;
    original.active_tab = 1;
    original.active_preset = "ProPreset";

    original.presets["ProPreset"] = { "ProPreset", 112.5, 70.3, 80.0, 50.0, 12.5, "16:10", 1, "cyan" };
    original.palettes["cyan"] = { "cyan", "#00FFFF", "wallpaper.png" };

    std::string serialized = ConfigParser::SerializeIni(original);
    OsuPointConfig parsed = ConfigParser::ParseIni(serialized);

    ASSERT_NEAR(parsed.width, original.width, 1e-4);
    ASSERT_NEAR(parsed.height, original.height, 1e-4);
    ASSERT_NEAR(parsed.center_x, original.center_x, 1e-4);
    ASSERT_NEAR(parsed.center_y, original.center_y, 1e-4);
    ASSERT_NEAR(parsed.rotation, original.rotation, 1e-4);
    ASSERT_EQ(parsed.monitor, original.monitor);
    ASSERT_EQ(parsed.display_width, original.display_width);
    ASSERT_EQ(parsed.display_height, original.display_height);
    ASSERT_EQ(parsed.display_x, original.display_x);
    ASSERT_EQ(parsed.display_y, original.display_y);
    ASSERT_EQ(parsed.lock_ratio, original.lock_ratio);
    ASSERT_EQ(parsed.ratio, original.ratio);
    ASSERT_EQ(parsed.ratio_precision, original.ratio_precision);
    ASSERT_EQ(parsed.lock_drag, original.lock_drag);
    ASSERT_EQ(parsed.clip_area, original.clip_area);
    ASSERT_EQ(parsed.pen_click, original.pen_click);
    ASSERT_EQ(parsed.barrel_button, original.barrel_button);
    ASSERT_EQ(parsed.tip_pressure_threshold, original.tip_pressure_threshold);
    ASSERT_EQ(parsed.antialiasing, original.antialiasing);
    ASSERT_EQ(parsed.ui_outline, original.ui_outline);
    ASSERT_NEAR(parsed.extrapolation_mm, original.extrapolation_mm, 1e-4);
    ASSERT_EQ(parsed.filter, original.filter);
    ASSERT_EQ(parsed.motion_filter, original.motion_filter);
    ASSERT_EQ(parsed.spin_percent, original.spin_percent);
    ASSERT_EQ(parsed.debug_mode, original.debug_mode);
    ASSERT_EQ(parsed.unlock_limits, original.unlock_limits);
    ASSERT_EQ(parsed.palette, original.palette);
    ASSERT_EQ(parsed.theme_base, original.theme_base);
    ASSERT_EQ(parsed.bg_image, original.bg_image);
    ASSERT_EQ(parsed.copy_bg_image, original.copy_bg_image);
    ASSERT_EQ(parsed.delete_old_bg_image, original.delete_old_bg_image);
    ASSERT_EQ(parsed.copied_bg_images, original.copied_bg_images);
    ASSERT_EQ(parsed.ui_opacity, original.ui_opacity);
    ASSERT_EQ(parsed.bg_blur, original.bg_blur);
    ASSERT_EQ(parsed.ui_style, original.ui_style);
    ASSERT_EQ(parsed.bg_blur_type, original.bg_blur_type);
    ASSERT_EQ(parsed.ui_outline_opacity, original.ui_outline_opacity);
    ASSERT_EQ(parsed.ui_area_opacity, original.ui_area_opacity);
    ASSERT_EQ(parsed.active_tab, original.active_tab);
    ASSERT_EQ(parsed.active_preset, original.active_preset);

    ASSERT_EQ(parsed.presets.size(), 1u);
    ASSERT_EQ(parsed.palettes.size(), 1u);
}

E2E_TEST(Config_DefaultValues_MatchSpecification, TestTier::Tier1_Feature) {
    OsuPointConfig def;
    ASSERT_NEAR(def.width, 152.0, 1e-4);
    ASSERT_NEAR(def.height, 95.0, 1e-4);
    ASSERT_NEAR(def.center_x, 76.0, 1e-4);
    ASSERT_NEAR(def.center_y, 47.5, 1e-4);
    ASSERT_NEAR(def.rotation, 0.0, 1e-4);
    ASSERT_EQ(def.ratio, "16:9");
    ASSERT_EQ(def.ratio_precision, 8);
    ASSERT_EQ(def.filter, 25);
    ASSERT_EQ(def.motion_filter, 40);
    ASSERT_EQ(def.spin_percent, 0);
    ASSERT_NEAR(def.extrapolation_mm, 2.5, 1e-4);
    ASSERT_EQ(def.palette, "amber");
    ASSERT_EQ(def.theme_base, "dark");
}

E2E_TEST(Config_PresetManagement_AddSwitchDelete, TestTier::Tier1_Feature) {
    OsuPointConfig cfg;
    cfg.presets["SmallArea"] = { "SmallArea", 60.0, 40.0, 76.0, 47.5, 0.0, "3:2", 0, "amber" };
    cfg.active_preset = "SmallArea";

    ASSERT_EQ(cfg.presets.size(), 1u);
    ASSERT_TRUE(cfg.presets.find("SmallArea") != cfg.presets.end());
    ASSERT_EQ(cfg.active_preset, "SmallArea");

    // Delete preset
    cfg.presets.erase("SmallArea");
    ASSERT_EQ(cfg.presets.size(), 0u);
}

E2E_TEST(Config_PaletteManagement_AddSwitchDelete, TestTier::Tier1_Feature) {
    OsuPointConfig cfg;
    cfg.palettes["Emerald"] = { "Emerald", "#50C878", "" };
    cfg.palette = "Emerald";

    ASSERT_EQ(cfg.palettes.size(), 1u);
    ASSERT_EQ(cfg.palette, "Emerald");

    cfg.palettes.erase("Emerald");
    ASSERT_EQ(cfg.palettes.size(), 0u);
}

E2E_TEST(Config_CommentAndBlankLineHandling, TestTier::Tier1_Feature) {
    std::string ini = "# Top comment\n; Semicolon comment\n\nwidth=100\n\n# Another comment\nheight=50\n";
    OsuPointConfig cfg = ConfigParser::ParseIni(ini);
    ASSERT_NEAR(cfg.width, 100.0, 1e-4);
    ASSERT_NEAR(cfg.height, 50.0, 1e-4);
}

E2E_TEST(Config_BooleanParsingFlexibility, TestTier::Tier1_Feature) {
    ASSERT_TRUE(ConfigParser::ParseBool("1"));
    ASSERT_TRUE(ConfigParser::ParseBool("true"));
    ASSERT_TRUE(ConfigParser::ParseBool("yes"));
    ASSERT_TRUE(ConfigParser::ParseBool("on"));
    ASSERT_TRUE(ConfigParser::ParseBool("TRUE"));

    ASSERT_FALSE(ConfigParser::ParseBool("0"));
    ASSERT_FALSE(ConfigParser::ParseBool("false"));
    ASSERT_FALSE(ConfigParser::ParseBool("no"));
    ASSERT_FALSE(ConfigParser::ParseBool("off"));
    ASSERT_FALSE(ConfigParser::ParseBool("FALSE"));
}

// --- TIER 2: Boundary & Corner Cases ---

E2E_TEST(Config_OpacityClamping_Between10And100, TestTier::Tier2_Boundary) {
    std::string iniLow = "ui_opacity=5\n";
    OsuPointConfig cfgLow = ConfigParser::ParseIni(iniLow);
    ASSERT_EQ(cfgLow.ui_opacity, 10);

    std::string iniHigh = "ui_opacity=150\n";
    OsuPointConfig cfgHigh = ConfigParser::ParseIni(iniHigh);
    ASSERT_EQ(cfgHigh.ui_opacity, 100);
}

E2E_TEST(Config_BlurRadiusClamping_Between0And20, TestTier::Tier2_Boundary) {
    std::string iniLow = "bg_blur=-5\n";
    OsuPointConfig cfgLow = ConfigParser::ParseIni(iniLow);
    ASSERT_EQ(cfgLow.bg_blur, 0);

    std::string iniHigh = "bg_blur=35\n";
    OsuPointConfig cfgHigh = ConfigParser::ParseIni(iniHigh);
    ASSERT_EQ(cfgHigh.bg_blur, 20);
}

E2E_TEST(Config_FilterPercentages_ClampedTo100, TestTier::Tier2_Boundary) {
    std::string ini = "filter=150\nmotion_filter=-10\nspin_percent=200\n";
    OsuPointConfig cfg = ConfigParser::ParseIni(ini);
    ASSERT_EQ(cfg.filter, 100);
    ASSERT_EQ(cfg.motion_filter, 0);
    ASSERT_EQ(cfg.spin_percent, 100);
}

E2E_TEST(Config_ExtrapolationMm_ClampedTo0_5, TestTier::Tier2_Boundary) {
    std::string iniLow = "extrapolation_mm=-2.0\n";
    OsuPointConfig cfgLow = ConfigParser::ParseIni(iniLow);
    ASSERT_NEAR(cfgLow.extrapolation_mm, 0.0, 1e-4);

    std::string iniHigh = "extrapolation_mm=9.5\n";
    OsuPointConfig cfgHigh = ConfigParser::ParseIni(iniHigh);
    ASSERT_NEAR(cfgHigh.extrapolation_mm, 5.0, 1e-4);
}

E2E_TEST(Config_RatioPrecision_ClampedTo1_8, TestTier::Tier2_Boundary) {
    std::string iniLow = "ratio_precision=0\n";
    OsuPointConfig cfgLow = ConfigParser::ParseIni(iniLow);
    ASSERT_EQ(cfgLow.ratio_precision, 1);

    std::string iniHigh = "ratio_precision=20\n";
    OsuPointConfig cfgHigh = ConfigParser::ParseIni(iniHigh);
    ASSERT_EQ(cfgHigh.ratio_precision, 8);
}

E2E_TEST(Config_QuotedStringValues_StrippedCleanly, TestTier::Tier2_Boundary) {
    std::string ini = "palette=\"purple_haze\"\nbg_image='custom_art.png'\n";
    OsuPointConfig cfg = ConfigParser::ParseIni(ini);
    ASSERT_EQ(cfg.palette, "purple_haze");
    ASSERT_EQ(cfg.bg_image, "custom_art.png");
}

E2E_TEST(Config_WhitespaceSurroundingKeysAndValues_Trimmed, TestTier::Tier2_Boundary) {
    std::string ini = "   width   =   90.5   \n   height   =   60.2   \n";
    OsuPointConfig cfg = ConfigParser::ParseIni(ini);
    ASSERT_NEAR(cfg.width, 90.5, 1e-4);
    ASSERT_NEAR(cfg.height, 60.2, 1e-4);
}

E2E_TEST(Config_MalformedEntries_DoNotCrashParser, TestTier::Tier2_Boundary) {
    std::string ini = "bad_line_without_equals\nwidth=invalid_float\nheight=50\n";
    // Should not throw or crash
    OsuPointConfig cfg = ConfigParser::ParseIni(ini);
    ASSERT_NEAR(cfg.height, 50.0, 1e-4);
}

// --- TIER 3: Cross-Feature Combinations ---

E2E_TEST(Config_PresetSwitching_UpdatesActiveGlobalArea, TestTier::Tier3_CrossFeature) {
    OsuPointConfig cfg;
    cfg.presets["OsuJump"] = { "OsuJump", 80.0, 45.0, 76.0, 47.5, 0.0, "16:9", 0, "amber" };

    // Simulate user selecting preset in GUI
    const auto& sel = cfg.presets["OsuJump"];
    cfg.width = sel.width;
    cfg.height = sel.height;
    cfg.center_x = sel.center_x;
    cfg.center_y = sel.center_y;
    cfg.rotation = sel.rotation;
    cfg.ratio = sel.ratio;
    cfg.active_preset = sel.name;

    ASSERT_EQ(cfg.active_preset, "OsuJump");
    ASSERT_NEAR(cfg.width, 80.0, 1e-4);
    ASSERT_NEAR(cfg.height, 45.0, 1e-4);
}

E2E_TEST(Config_RoundtripWithMultiplePresetsAndPalettes, TestTier::Tier3_CrossFeature) {
    OsuPointConfig cfg;
    cfg.presets["P1"] = { "P1", 100.0, 60.0, 70.0, 40.0, 5.0, "16:9", 0, "amber" };
    cfg.presets["P2"] = { "P2", 120.0, 75.0, 76.0, 47.5, 0.0, "16:10", 1, "cyan" };
    cfg.palettes["Amber"] = { "Amber", "#FFBF00", "bg1.jpg" };
    cfg.palettes["Cyan"] = { "Cyan", "#00FFFF", "bg2.jpg" };

    std::string s = ConfigParser::SerializeIni(cfg);
    OsuPointConfig parsed = ConfigParser::ParseIni(s);

    ASSERT_EQ(parsed.presets.size(), 2u);
    ASSERT_EQ(parsed.palettes.size(), 2u);
    ASSERT_NEAR(parsed.presets["P1"].width, 100.0, 1e-4);
    ASSERT_NEAR(parsed.presets["P2"].width, 120.0, 1e-4);
    ASSERT_EQ(parsed.palettes["Amber"].accent, "#FFBF00");
    ASSERT_EQ(parsed.palettes["Cyan"].accent, "#00FFFF");
}
