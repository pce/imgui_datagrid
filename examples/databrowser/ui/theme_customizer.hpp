#pragma once

#include "imgui.h"
#include "theme.hpp"
#include "../io/rfs.hpp"
#include <array>
#include <filesystem>
#include <functional>

namespace datagrid::ui {

// ── ThemePalette ─────────────────────────────────────────────────────────────
//
//  Eight semantic color slots that drive all ImGui colors for a theme.
//
//    bg0  — darkest bg  (WindowBg, TitleBg, MenuBarBg, ScrollbarBg)
//    bg1  — mid bg      (ChildBg, FrameBg)
//    bg2  — hover bg    (FrameBgHovered)
//    fg0  — primary text
//    fg1  — disabled / secondary text
//    brd  — borders and scrollbar grabs
//    acc  — primary accent (checkmark, separator active, …)
//    acc2 — secondary / dimmer accent (button base, header, …)
//
struct ThemePalette {
    ImVec4 bg0  = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
    ImVec4 bg1  = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    ImVec4 bg2  = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    ImVec4 fg0  = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    ImVec4 fg1  = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    ImVec4 brd  = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    ImVec4 acc  = ImVec4(0.20f, 0.70f, 0.40f, 1.00f);
    ImVec4 acc2 = ImVec4(0.10f, 0.40f, 0.25f, 1.00f);
};

// ── ThemeFonts ────────────────────────────────────────────────────────────────
//
//  Per-theme font pair.  Loaded from JSON ("font.main", "font.mono",
//  "font.icon").  Empty path = use built-in code default.
//
struct ThemeFonts {
    FontConfig ui;   ///< proportional sans-serif UI font (FA icons merged into it)
    FontConfig mono; ///< monospace code / hex-viewer font
    FontConfig icon; ///< icon font (Font Awesome solid)
};

// ── ThemeCustomizer ───────────────────────────────────────────────────────────
//
//  Renders a live color-picker panel for the eight semantic palette slots and
//  style tweaks.  Serialises each theme to  #ThemeName.json  files so overrides
//  survive restarts.
//
class ThemeCustomizer {
public:
    /// Called just before apply() when the active theme changes via the UI.
    /// Use this to lazy-load the new theme's fonts before apply() sets them.
    std::function<void(ThemeStyle)> onThemeChanged;

    // Open / close the customizer window.
    void Toggle() { show_ = !show_; }
    bool IsVisible() const { return show_; }

    // Render the customizer window.  Returns true when a color / style changed.
    bool Render(Theme& theme, ThemeStyle& active);

    // Apply palette + style for theme t to the live ImGui state.
    void apply(Theme& theme, ThemeStyle t) const;

    // Derive all ImGui colors from a palette (public / static — usable standalone).
    static void palette_to_imgui(const ThemePalette& p, ImVec4* colors);

    // Apply a StyleParams to ImGuiStyle (rounding, spacing, padding).
    static void apply_style(const StyleParams& p);

    // Accessors for per-theme data.
    ThemePalette&       palette(ThemeStyle t)       { return palettes_[idx(t)]; }
    const ThemePalette& palette(ThemeStyle t) const { return palettes_[idx(t)]; }
    StyleParams&        style  (ThemeStyle t)       { return styles_  [idx(t)]; }
    const StyleParams&  style  (ThemeStyle t) const { return styles_  [idx(t)]; }
    ThemeFonts&         fonts  (ThemeStyle t)       { return fonts_   [idx(t)]; }
    const ThemeFonts&   fonts  (ThemeStyle t) const { return fonts_   [idx(t)]; }

    // Reset a single theme's palette + style to the compiled-in defaults.
    void reset_to_builtin(ThemeStyle t);

    // ── Serialisation ────────────────────────────────────────────────────────
    // Built-in definitions:  <themes_dir>/{id}.json   (no prefix, RO resource)
    // User overrides:        <themes_dir>/#{id}.json  (# prefix, RW config)
    // Missing files are silently skipped (compiled-in palette used instead).

    /// Load built-in palette/style/fonts for non-default themes from {id}.json.
    /// SolarizedDark (0) and SolarizedLight (1) always come from code.
    /// Returns the number of files successfully loaded.
    int  load_builtin_definitions(const std::filesystem::path& themes_dir, const io::Rfs& rfs);

    /// Load all #ThemeName.json user overrides present in themes_dir.
    /// Returns the number of files successfully loaded.
    /// Skips silently when themes_dir is not readable according to rfs.
    int  load_all(const std::filesystem::path& themes_dir, const io::Rfs& rfs);

    /// Save every theme's palette + style + fonts to themes_dir.
    /// Returns false if themes_dir is not writable according to rfs.
    bool save_all(const std::filesystem::path& themes_dir, const io::Rfs& rfs) const;

    /// Save a single theme's palette + style + fonts.
    /// Returns false if themes_dir is not writable according to rfs.
    bool save_theme(ThemeStyle t, const std::filesystem::path& themes_dir, const io::Rfs& rfs) const;

private:
    static int  idx(ThemeStyle t) { return static_cast<int>(t); }

    static std::filesystem::path theme_file(ThemeStyle t, const std::filesystem::path& dir);

    static std::array<ThemePalette, kThemeCount> builtin_palettes() noexcept;
    static std::array<StyleParams,  kThemeCount> builtin_styles()   noexcept;
    /// Default font pair for each theme (derived from default_main_font() and
    /// per-theme knowledge of whether the UI font is proportional or monospace).
    static std::array<ThemeFonts,   kThemeCount> builtin_fonts()    noexcept;

    std::array<ThemePalette, kThemeCount> palettes_ = builtin_palettes();
    std::array<StyleParams,  kThemeCount> styles_   = builtin_styles();
    std::array<ThemeFonts,   kThemeCount> fonts_    = builtin_fonts();

    // Render state
    bool       show_         = false;
    ThemeStyle preview_type_ = ThemeStyle::SolarizedDark;
};

} // namespace datagrid::ui
