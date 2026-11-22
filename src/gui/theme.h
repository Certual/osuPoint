#pragma once

#include <string>
#include <vector>
#include <imgui.h>

namespace osupoint::gui {

struct ThemePalette {
    std::string name;
    ImVec4 accent;
};

class Theme {
public:
    static void Apply(const std::string& theme_base,
                      const std::string& palette_name,
                      int opacity_percent = 100,
                      const std::string& custom_hex = "");

    static ImVec4 GetAccentColor(const std::string& palette_name, const std::string& custom_hex = "");
    static ImVec4 HexToColor(const std::string& hex, float alpha = 1.0f);
    static std::string ColorToHex(const ImVec4& col);

    static const std::vector<std::string>& GetBuiltinPalettes();
};

} // namespace osupoint::gui
