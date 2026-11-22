#include "theme.h"
#include <algorithm>
#include <cstdio>

namespace osupoint::gui {

static const std::vector<std::string> s_builtinPalettes = {
    "system", "amber", "cyan", "emerald", "pink", "violet", "mono"
};

const std::vector<std::string>& Theme::GetBuiltinPalettes() {
    return s_builtinPalettes;
}

ImVec4 Theme::HexToColor(const std::string& hex, float alpha) {
    if (hex.empty()) {
        return ImVec4(1.0f, 0.70f, 0.0f, alpha);
    }

    size_t start = 0;
    if (hex[0] == '#') start = 1;

    unsigned int r = 255, g = 176, b = 0;
    if (hex.size() >= start + 6) {
        std::sscanf(hex.c_str() + start, "%02x%02x%02x", &r, &g, &b);
    }

    return ImVec4(static_cast<float>(r) / 255.0f,
                  static_cast<float>(g) / 255.0f,
                  static_cast<float>(b) / 255.0f,
                  alpha);
}

std::string Theme::ColorToHex(const ImVec4& col) {
    char buf[16];
    int r = std::clamp(static_cast<int>(col.x * 255.0f), 0, 255);
    int g = std::clamp(static_cast<int>(col.y * 255.0f), 0, 255);
    int b = std::clamp(static_cast<int>(col.z * 255.0f), 0, 255);
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
    return std::string(buf);
}

ImVec4 Theme::GetAccentColor(const std::string& palette_name, const std::string& custom_hex) {
    if (!custom_hex.empty() && custom_hex[0] == '#') {
        return HexToColor(custom_hex);
    }

    if (palette_name == "amber") {
        return ImVec4(1.0f, 0.70f, 0.0f, 1.0f);       // #FFB300 Golden Amber
    } else if (palette_name == "cyan") {
        return ImVec4(0.0f, 0.82f, 1.0f, 1.0f);       // #00D2FF Ice Cyan
    } else if (palette_name == "emerald") {
        return ImVec4(0.0f, 0.90f, 0.46f, 1.0f);      // #00E676 Emerald
    } else if (palette_name == "pink") {
        return ImVec4(1.0f, 0.40f, 0.67f, 1.0f);      // #FF66AA Sakura Pink
    } else if (palette_name == "violet") {
        return ImVec4(0.66f, 0.33f, 0.97f, 1.0f);     // #A855F7 Sunset Violet
    } else if (palette_name == "mono") {
        return ImVec4(0.94f, 0.94f, 0.96f, 1.0f);     // #F0F0F5 Monochrome
    } else if (palette_name == "light") {
        return ImVec4(0.0f, 0.47f, 0.84f, 1.0f);      // #0078D6 Classic Azure
    } else if (palette_name == "dark" || palette_name == "system") {
        return ImVec4(0.0f, 0.47f, 0.84f, 1.0f);      // #0078D6 Upstream Default Blue
    }

    return ImVec4(0.0f, 0.47f, 0.84f, 1.0f);
}

void Theme::Apply(const std::string& theme_base,
                  const std::string& palette_name,
                  int opacity_percent,
                  const std::string& custom_hex) {
    ImGuiStyle& style = ImGui::GetStyle();

    // Rounding & metrics matching upstream Fluent / osuPoint UI
    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 8.0f;
    style.FrameRounding     = 5.0f;
    style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 6.0f;

    style.WindowBorderSize  = 0.0f;
    style.ChildBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;

    style.WindowPadding     = ImVec2(10.0f, 10.0f);
    style.FramePadding      = ImVec2(6.0f, 3.5f);
    style.ItemSpacing       = ImVec2(6.0f, 5.0f);
    style.ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
    style.ScrollbarSize     = 10.0f;

    const float card_alpha = std::clamp(static_cast<float>(opacity_percent) / 100.0f, 0.10f, 1.0f);
    const ImVec4 accent = GetAccentColor(palette_name, custom_hex);

    // Accent variations (+18% hover, -18% active)
    const ImVec4 accent_hover = ImVec4(
        std::min(1.0f, accent.x * 1.18f),
        std::min(1.0f, accent.y * 1.18f),
        std::min(1.0f, accent.z * 1.18f),
        1.0f
    );
    const ImVec4 accent_active = ImVec4(
        accent.x * 0.82f,
        accent.y * 0.82f,
        accent.z * 0.82f,
        1.0f
    );
    const ImVec4 accent_subtle = ImVec4(accent.x, accent.y, accent.z, 0.22f * card_alpha);

    ImVec4* colors = style.Colors;
    const bool is_light = (theme_base == "light");

    if (is_light) {
        colors[ImGuiCol_Text]                  = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
        colors[ImGuiCol_TextDisabled]          = ImVec4(0.35f, 0.35f, 0.42f, 1.00f);
        colors[ImGuiCol_WindowBg]              = ImVec4(0.95f, 0.95f, 0.97f, 1.00f);
        colors[ImGuiCol_ChildBg]               = ImVec4(1.00f, 1.00f, 1.00f, card_alpha);
        colors[ImGuiCol_PopupBg]               = ImVec4(1.00f, 1.00f, 1.00f, 0.98f);
        colors[ImGuiCol_Border]                = ImVec4(0.80f, 0.80f, 0.85f, 0.80f);
        colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]               = ImVec4(0.92f, 0.92f, 0.95f, 0.90f);
        colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.86f, 0.88f, 0.92f, 1.00f);
        colors[ImGuiCol_FrameBgActive]         = ImVec4(0.80f, 0.82f, 0.88f, 1.00f);
        colors[ImGuiCol_TitleBg]               = ImVec4(0.95f, 0.95f, 0.97f, 1.00f);
        colors[ImGuiCol_TitleBgActive]         = ImVec4(0.95f, 0.95f, 0.97f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.95f, 0.95f, 0.97f, 0.75f);
        colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.95f, 0.95f, 0.97f, 0.60f);
        colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.72f, 0.75f, 0.80f, 0.80f);
        colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.62f, 0.66f, 0.72f, 0.90f);
        colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.50f, 0.55f, 0.62f, 1.00f);
        colors[ImGuiCol_CheckMark]             = accent;
        colors[ImGuiCol_SliderGrab]            = accent;
        colors[ImGuiCol_SliderGrabActive]      = accent_active;
        colors[ImGuiCol_Button]                = ImVec4(0.92f, 0.92f, 0.95f, 0.90f);
        colors[ImGuiCol_ButtonHovered]         = ImVec4(0.86f, 0.88f, 0.92f, 1.00f);
        colors[ImGuiCol_ButtonActive]          = accent_active;
        colors[ImGuiCol_Header]                = accent_subtle;
        colors[ImGuiCol_HeaderHovered]         = ImVec4(accent.x, accent.y, accent.z, 0.35f);
        colors[ImGuiCol_HeaderActive]          = ImVec4(accent.x, accent.y, accent.z, 0.50f);
        colors[ImGuiCol_Separator]             = ImVec4(0.80f, 0.80f, 0.85f, 0.60f);
        colors[ImGuiCol_SeparatorHovered]      = accent_hover;
        colors[ImGuiCol_SeparatorActive]       = accent_active;
        colors[ImGuiCol_Tab]                   = ImVec4(0.90f, 0.90f, 0.94f, 0.75f);
        colors[ImGuiCol_TabHovered]            = accent_hover;
        colors[ImGuiCol_TabSelected]           = accent;
        colors[ImGuiCol_TabDimmed]             = ImVec4(0.92f, 0.92f, 0.95f, 0.60f);
        colors[ImGuiCol_TabDimmedSelected]     = ImVec4(accent.x, accent.y, accent.z, 0.80f);
    } else {
        // Dark theme: 1:1 tokens from osuPoint upstream
        colors[ImGuiCol_Text]                  = ImVec4(245.0f/255.0f, 245.0f/255.0f, 250.0f/255.0f, 1.00f); // #F5F5FA
        colors[ImGuiCol_TextDisabled]          = ImVec4(185.0f/255.0f, 185.0f/255.0f, 200.0f/255.0f, 1.00f); // #B9B9C8
        colors[ImGuiCol_WindowBg]              = ImVec4(14.0f/255.0f, 14.0f/255.0f, 17.0f/255.0f, 1.00f);     // #0E0E11
        colors[ImGuiCol_ChildBg]               = ImVec4(22.0f/255.0f, 22.0f/255.0f, 26.0f/255.0f, card_alpha); // #16161A
        colors[ImGuiCol_PopupBg]               = ImVec4(22.0f/255.0f, 22.0f/255.0f, 28.0f/255.0f, 0.98f);
        colors[ImGuiCol_Border]                = ImVec4(40.0f/255.0f, 40.0f/255.0f, 48.0f/255.0f, 1.00f);     // #282830
        colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]               = ImVec4(30.0f/255.0f, 30.0f/255.0f, 36.0f/255.0f, 1.00f);     // #1E1E24
        colors[ImGuiCol_FrameBgHovered]        = ImVec4(42.0f/255.0f, 42.0f/255.0f, 52.0f/255.0f, 1.00f);     // #2A2A34
        colors[ImGuiCol_FrameBgActive]         = ImVec4(48.0f/255.0f, 48.0f/255.0f, 60.0f/255.0f, 1.00f);
        colors[ImGuiCol_TitleBg]               = ImVec4(14.0f/255.0f, 14.0f/255.0f, 17.0f/255.0f, 1.00f);
        colors[ImGuiCol_TitleBgActive]         = ImVec4(14.0f/255.0f, 14.0f/255.0f, 17.0f/255.0f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(14.0f/255.0f, 14.0f/255.0f, 17.0f/255.0f, 0.75f);
        colors[ImGuiCol_ScrollbarBg]           = ImVec4(18.0f/255.0f, 18.0f/255.0f, 22.0f/255.0f, 0.60f);
        colors[ImGuiCol_ScrollbarGrab]         = ImVec4(40.0f/255.0f, 40.0f/255.0f, 48.0f/255.0f, 0.80f);
        colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(52.0f/255.0f, 52.0f/255.0f, 62.0f/255.0f, 0.90f);
        colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(64.0f/255.0f, 64.0f/255.0f, 76.0f/255.0f, 1.00f);
        colors[ImGuiCol_CheckMark]             = accent;
        colors[ImGuiCol_SliderGrab]            = accent;
        colors[ImGuiCol_SliderGrabActive]      = accent_active;
        colors[ImGuiCol_Button]                = ImVec4(30.0f/255.0f, 30.0f/255.0f, 36.0f/255.0f, 1.00f);     // #1E1E24
        colors[ImGuiCol_ButtonHovered]         = ImVec4(42.0f/255.0f, 42.0f/255.0f, 52.0f/255.0f, 1.00f);     // #2A2A34
        colors[ImGuiCol_ButtonActive]          = accent_active;
        colors[ImGuiCol_Header]                = accent_subtle;
        colors[ImGuiCol_HeaderHovered]         = ImVec4(accent.x, accent.y, accent.z, 0.35f);
        colors[ImGuiCol_HeaderActive]          = ImVec4(accent.x, accent.y, accent.z, 0.50f);
        colors[ImGuiCol_Separator]             = ImVec4(40.0f/255.0f, 40.0f/255.0f, 48.0f/255.0f, 0.60f);
        colors[ImGuiCol_SeparatorHovered]      = accent_hover;
        colors[ImGuiCol_SeparatorActive]       = accent_active;
        colors[ImGuiCol_Tab]                   = ImVec4(26.0f/255.0f, 26.0f/255.0f, 32.0f/255.0f, 1.00f);
        colors[ImGuiCol_TabHovered]            = ImVec4(34.0f/255.0f, 34.0f/255.0f, 42.0f/255.0f, 1.00f);
        colors[ImGuiCol_TabSelected]           = ImVec4(36.0f/255.0f, 36.0f/255.0f, 44.0f/255.0f, 1.00f);
        colors[ImGuiCol_TabDimmed]             = ImVec4(22.0f/255.0f, 22.0f/255.0f, 26.0f/255.0f, 1.00f);
        colors[ImGuiCol_TabDimmedSelected]     = ImVec4(36.0f/255.0f, 36.0f/255.0f, 44.0f/255.0f, 1.00f);
    }
}

} // namespace osupoint::gui
