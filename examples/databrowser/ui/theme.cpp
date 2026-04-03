#include "theme.hpp"
#include "imgui.h"


namespace datagrid::ui {
    // ── theme_from_id ─────────────────────────────────────────────────────────────

    ThemeStyle theme_from_id(std::string_view id) noexcept {
        using enum ThemeStyle;
        if (id == "SolarizedDark")      return SolarizedDark;
        if (id == "SolarizedLight")     return SolarizedLight;
        if (id == "Monokai")            return Monokai;
        if (id == "MonokaiDark")        return MonokaiDark;
        if (id == "MonaSpaces")         return MonaSpaces;
        if (id == "EarthSUSE")          return EarthSUSE;
        if (id == "EarthSUSEDark")      return EarthSUSEDark;
        if (id == "NeonSpaces")         return NeonSpaces;
        if (id == "DawnBringer16Dark")  return DawnBringer16Dark;
        if (id == "DawnBringer16Light") return DawnBringer16Light;
        if (id == "Material")           return Material;
        if (id == "MaterialDark")       return MaterialDark;
        if (id == "MonoLight")          return MonoLight;
        if (id == "MonoDark")           return MonoDark;
        if (id == "DawnBringerLight")   return DawnBringerLight;
        if (id == "DawnBringerDark")    return DawnBringerDark;
        return SolarizedDark;
    }

    // ── default_main_font / default_icon_font ─────────────────────────────────────
    //
    //  Paths are Rfs-relative (resolved against the resources/ mount, i.e.
    //  "fonts/Foo.ttf" → <exe>/resources/fonts/Foo.ttf).
    //

    FontConfig default_main_font(ThemeStyle t) noexcept {
        using enum ThemeStyle;
        switch (t) {
            case MonaSpaces:
                return { "fonts/MonaspaceArgonNF-Regular.otf", 15.f };
            case EarthSUSE:
            case EarthSUSEDark:
                return { "fonts/SUSE-Light.ttf", 15.f };
            case NeonSpaces:
                return { "fonts/MonaspaceArgonNF-Regular.otf", 15.f };
            case DawnBringer16Dark:
            case DawnBringer16Light:
                return { "fonts/MonaspaceKryptonNF-WideLight.otf", 13.f };
            case MonoLight:
            case MonoDark:
                return { "fonts/JetBrainsMono-Thin.ttf", 14.f };
            case DawnBringerLight:
            case DawnBringerDark:
                return { "fonts/Hack-Regular.ttf", 14.f };
            default:
                return { "fonts/Roboto-Medium.ttf", 15.f };
        }
    }

    FontConfig default_icon_font(ThemeStyle t) noexcept {
        (void)t;
        return { "fonts/fa-6-solid-900.otf", 14.f };
    }

    // ── ApplyThemeStyle_ ──────────────────────────────────────────────────────────
    //
    //  Built-in per-theme layout defaults (mirrors ThemeCustomizer::builtin_styles).
    //

    static StyleParams builtin_style(ThemeStyle t) noexcept {
        using enum ThemeStyle;
        switch (t) {
            case MonaSpaces:
                return { 10.f, 10.f, 10.f, 1.f };
            case EarthSUSE:
            case EarthSUSEDark:
                return {  5.f,  8.f,  6.f, 1.f };
            case NeonSpaces:
                return {  2.f,  8.f,  6.f, 1.f };
            case DawnBringer16Dark:
            case DawnBringer16Light:
            case DawnBringerLight:
            case DawnBringerDark:
                return {  0.f, 10.f,  8.f, 1.f };
            case Material:
            case MaterialDark:
                return {  8.f,  8.f,  8.f, 1.f };
            case MonoLight:
            case MonoDark:
                return {  2.f,  6.f,  4.f, 1.f };
            default: // SolarizedDark/Light, Monokai, MonokaiDark
                return {  4.f,  8.f,  6.f, 1.f };
        }
    }

    static void apply_style_params(const StyleParams& p, float dpi) noexcept {
        const float s = (dpi > 0.f) ? dpi : 1.f;
        ImGuiStyle& st = ImGui::GetStyle();
        st.WindowRounding    = p.rounding    * s;
        st.FrameRounding     = p.rounding    * s;
        st.ChildRounding     = p.rounding    * s;
        st.PopupRounding     = p.rounding    * s;
        st.TabRounding       = p.rounding    * s;
        st.ScrollbarRounding = p.rounding    * s;
        st.GrabRounding      = p.rounding    * s;
        st.ItemSpacing       = { p.item_spacing  * s, p.item_spacing  * s * 0.50f };
        st.FramePadding      = { p.frame_padding * s, p.frame_padding * s * 0.667f };
    }

    void Theme::ApplyThemeStyle_(ThemeStyle t) const {
        apply_style_params(builtin_style(t), dpiScale_);
    }

    // ── ApplyImGuiStyle ───────────────────────────────────────────────────────────

    void Theme::ApplyImGuiStyle(float dpiScale) {
        dpiScale_ = (dpiScale > 0.0f) ? dpiScale : 1.0f;
        ApplyThemeStyle_(current_);
    }

    void Theme::ApplyImGuiStyle(float dpiScale, const StyleParams& customStyle) {
        dpiScale_ = (dpiScale > 0.0f) ? dpiScale : 1.0f;
        apply_style_params(customStyle, dpiScale_);
    }

    // ── RegisterFonts ─────────────────────────────────────────────────────────────

    void Theme::RegisterFonts(ThemeStyle t, ImFont* uiFont, ImFont* monoFont) {
        const auto i  = static_cast<std::size_t>(t);
        uiFonts_[i]   = uiFont;
        codeFonts_[i] = monoFont;
    }

    // ── ApplyColorTheme ───────────────────────────────────────────────────────────

    void Theme::ApplyColorTheme(ThemeStyle theme) {
        // Switch UI and code fonts.
        const auto i = static_cast<std::size_t>(theme);
        if (uiFonts_[i])   ImGui::GetIO().FontDefault = uiFonts_[i];
        if (codeFonts_[i]) codeFont                   = codeFonts_[i];

        current_ = theme;
        ApplyThemeStyle_(theme);

        // Color assignment is delegated to ThemeCustomizer::palette_to_imgui
        // (called by ThemeCustomizer::apply).  A bare Theme::ApplyColorTheme only
        // handles fonts and layout — the palette comes from the customizer.
    }

} // namespace datagrid::ui
