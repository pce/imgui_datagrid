#pragma once

#include "imgui.h"
#include <array>

enum class ThemeType
{
    SolarizedDark,       //  0 — Solarized dark;          Roboto + Hack
    SolarizedLight,      //  1 — Solarized light;         Roboto + Hack
    Monokai,             //  2 — Monokai classic dark;    Roboto + Hack
    MonokaiDark,         //  3 — Monokai Pro (deeper);    Roboto + Hack
    MonaSpaces,          //  4 — Botanical/floral light;  Karla  + MonaspaceKrypton
    EarthSUSE,           //  5 — Earth warm + SUSE green; SUSE   + SUSEMono
    EarthSUSEDark,       //  6 — Earth deep dark + SUSE;  SUSE   + SUSEMono
    NeonSpaces,          //  7 — Electric neon dark;      Roboto + MonaspaceArgon
    DawnBringer16Dark,   //  8 — DB16 dark;               Hack   + Hack
    DawnBringer16Light,  //  9 — DB16 light;              Hack   + Hack
    Material,            // 10 — Material Design light;   Roboto + Hack
    MaterialDark,        // 11 — Material Design dark;    Roboto + Hack
    MonoLight,           // 12 — Minimal mono light;      JetBrainsMono + JetBrainsMono
    MonoDark,            // 13 — Minimal mono dark;       JetBrainsMono + JetBrainsMono
    DawnBringerLight,    // 14 — DB16 light + Monaspace;  Hack + MonaspaceArgon
    DawnBringerDark,     // 15 — DB16 dark  + Monaspace;  Hack + MonaspaceArgon
};

inline constexpr int kThemeCount = 16;

class Theme
{
  public:
    /// Updated by ApplyColorTheme(); read by TextArea::SetCodeFont().
    ImFont* codeFont = nullptr;

    /// Re-applies spacing/rounding for the current theme at the given DPI.
    /// Call once at startup (before ApplyColorTheme) and on DPI changes.
    void ApplyImGuiStyle(float dpiScale = 1.0f);

    /// Applies color palette AND per-theme spacing/rounding for ThemeType t.
    void ApplyColorTheme(ThemeType theme);

    /// Call once per theme from App::Init() after the font atlas is built.
    void RegisterFonts(ThemeType t, ImFont* uiFont, ImFont* monoFont);

  private:
    /// Per-theme spacing, rounding, border sizes — called by both public methods.
    void ApplyThemeStyle_(ThemeType t);

    float     dpiScale_ = 1.0f;
    ThemeType current_  = ThemeType::SolarizedDark;

    std::array<ImFont*, kThemeCount> uiFonts_   = {};
    std::array<ImFont*, kThemeCount> codeFonts_ = {};
};
