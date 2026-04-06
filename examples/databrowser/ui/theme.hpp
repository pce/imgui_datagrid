#pragma once

#include "imgui.h"
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace datagrid::ui {

//  One enumerator per built-in theme.  The integer value is also the array
//  index used throughout Theme, ThemeCustomizer, and Settings.
//
enum class ThemeStyle : int {
    SolarizedDark      =  0,
    SolarizedLight     =  1,
    Monokai            =  2,
    MonokaiDark        =  3,
    MonaSpaces         =  4,
    EarthSUSE          =  5,
    EarthSUSEDark      =  6,
    NeonSpaces         =  7,
    DawnBringer16Dark  =  8,
    DawnBringer16Light =  9,
    Material           = 10,
    MaterialDark       = 11,
    MonoLight          = 12,
    MonoDark           = 13,
    DawnBringerLight   = 14,
    DawnBringerDark    = 15,
};

inline constexpr int kThemeCount = 16;

/// Returns the stable JSON/file-name ID string for a theme.
[[nodiscard]] constexpr std::string_view theme_id(ThemeStyle t) noexcept {
    switch (t) {
        using enum ThemeStyle;
        case SolarizedDark:      return "SolarizedDark";
        case SolarizedLight:     return "SolarizedLight";
        case Monokai:            return "Monokai";
        case MonokaiDark:        return "MonokaiDark";
        case MonaSpaces:         return "MonaSpaces";
        case EarthSUSE:          return "EarthSUSE";
        case EarthSUSEDark:      return "EarthSUSEDark";
        case NeonSpaces:         return "NeonSpaces";
        case DawnBringer16Dark:  return "DawnBringer16Dark";
        case DawnBringer16Light: return "DawnBringer16Light";
        case Material:           return "Material";
        case MaterialDark:       return "MaterialDark";
        case MonoLight:          return "MonoLight";
        case MonoDark:           return "MonoDark";
        case DawnBringerLight:   return "DawnBringerLight";
        case DawnBringerDark:    return "DawnBringerDark";
    }
    return "SolarizedDark";
}

/// Reverse lookup — returns SolarizedDark for unrecognised ids.
[[nodiscard]] ThemeStyle theme_from_id(std::string_view id) noexcept;

/// List all .ttf/.otf fonts in resources/fonts/ (RFS-relative paths like "fonts/Hack-Regular.ttf").
[[nodiscard]] std::vector<std::string> enumerate_fonts() noexcept;


struct StyleParams {
    float rounding      = 4.f;   // Window/Frame/Child/Popup/Tab rounding
    float item_spacing  = 8.f;   // ItemSpacing.x  (y = x * 0.5)
    float frame_padding = 6.f;   // FramePadding.x (y = x * 0.667)
    float font_scale    = 1.f;   // reserved — not applied at runtime yet
};


struct FontConfig {
    std::string path;            ///< Rfs-relative path; empty = theme default
    float       size_px = 0.f;  ///< 0 = use theme default size
};

/// Built-in main font for each theme (Rfs-relative path + size).
[[nodiscard]] FontConfig default_main_font(ThemeStyle t) noexcept;

/// Built-in icon font for each theme.
[[nodiscard]] FontConfig default_icon_font(ThemeStyle t) noexcept;

//  The slice of theme state persisted to settings.json.

struct ThemeSettings {
    ThemeStyle  active      = ThemeStyle::SolarizedDark;
    StyleParams style;      ///< user-customised style for active theme
    FontConfig  main_font;  ///< override; empty = use default_main_font(active)
    FontConfig  icon_font;  ///< override; empty = use default_icon_font(active)
    FontConfig  hex_font;   ///< override for HexViewDialog; empty = code_font
};


class Theme {
public:
    /// Updated by ApplyColorTheme(); read by TextArea::SetCodeFont().
    ImFont* codeFont = nullptr;

    /// Hexview font (nullptr = use codeFont).
    ImFont* hexFont = nullptr;

    /// Store DPI scale and apply built-in per-theme spacing for the current theme.
    void ApplyImGuiStyle(float dpiScale = 1.0f);

    /// Same but applies customStyle instead of the built-in per-theme defaults.
    void ApplyImGuiStyle(float dpiScale, const StyleParams& customStyle);

    /// Applies color palette AND per-theme spacing/rounding for ThemeStyle t.
    void ApplyColorTheme(ThemeStyle theme);

    /// Call once per theme from App::Init() after the font atlas is built.
    void RegisterFonts(ThemeStyle t, ImFont* uiFont, ImFont* monoFont);

private:
    /// Applies built-in spacing/rounding defaults for theme t.
    void ApplyThemeStyle_(ThemeStyle t) const;

    float      dpiScale_ = 1.0f;
    ThemeStyle current_  = ThemeStyle::SolarizedDark;

    std::array<ImFont*, kThemeCount> uiFonts_   = {};
    std::array<ImFont*, kThemeCount> codeFonts_ = {};
};

} // namespace datagrid::ui

