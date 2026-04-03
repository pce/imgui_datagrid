#include "theme.hpp"

// Stores DPI and re-applies per-theme spacing/rounding for the current theme.
void Theme::ApplyImGuiStyle(float dpiScale)
{
    dpiScale_ = (dpiScale > 0.0f) ? dpiScale : 1.0f;
    ApplyThemeStyle_(current_);
}

// Per-theme spacing, rounding, and border sizes.
//
// Groups and their rationale:
//   Classic  (Solarized ×2, Monokai ×2)    — 4 px, balanced
//   Earth    (EarthSUSE ×2)                — 5 px, organic warmth
//   Botanical (MonaSpaces)                 — 10 px, generous, floral
//   Material (Material ×2)                 — 8 px, MD3 spec, no borders
//   Neon     (NeonSpaces)                  — 2 px, sharp scanline
//   Retro    (DawnBringer* ×4)             — 0 px, crisp pixel-art
//   Mono     (MonoLight, MonoDark)         — 2 px, dense programmer layout
void Theme::ApplyThemeStyle_(ThemeType t) const
{
    const float s  = dpiScale_;
    ImGuiStyle& st = ImGui::GetStyle();

    // safe border defaults
    st.WindowBorderSize = 1.0f;
    st.ChildBorderSize  = 1.0f;
    st.PopupBorderSize  = 1.0f;
    st.FrameBorderSize  = 0.0f;
    st.TabBorderSize    = 0.0f;

    switch (t) {

    // ── Botanical / Floral ────────────────────────────────────────────────────
    case ThemeType::MonaSpaces:
        st.WindowRounding    = 10.0f * s;
        st.FrameRounding     =  8.0f * s;
        st.ChildRounding     =  8.0f * s;
        st.PopupRounding     = 10.0f * s;
        st.TabRounding       =  8.0f * s;
        st.ScrollbarRounding = 10.0f * s;
        st.GrabRounding      =  8.0f * s;
        st.ScrollbarSize     = 16.0f * s;
        st.GrabMinSize       = 12.0f * s;
        st.WindowPadding     = { 14.0f * s, 14.0f * s };
        st.FramePadding      = { 10.0f * s,  6.0f * s };
        st.ItemSpacing       = { 10.0f * s,  6.0f * s };
        st.ItemInnerSpacing  = {  6.0f * s,  6.0f * s };
        st.CellPadding       = {  6.0f * s,  4.0f * s };
        st.IndentSpacing     = 24.0f * s;
        st.FrameBorderSize   = 1.0f;
        break;

    // ── Retro pixel-art ───────────────────────────────────────────────────────
    case ThemeType::DawnBringer16Dark:
    case ThemeType::DawnBringer16Light:
    case ThemeType::DawnBringerDark:
    case ThemeType::DawnBringerLight:
        st.WindowRounding    = 0.0f;
        st.FrameRounding     = 0.0f;
        st.ChildRounding     = 0.0f;
        st.PopupRounding     = 0.0f;
        st.TabRounding       = 0.0f;
        st.ScrollbarRounding = 0.0f;
        st.GrabRounding      = 0.0f;
        st.ScrollbarSize     = 14.0f * s;
        st.GrabMinSize       = 10.0f * s;
        st.WindowPadding     = { 12.0f * s, 10.0f * s };
        st.FramePadding      = {  8.0f * s,  5.0f * s };
        st.ItemSpacing       = { 10.0f * s,  5.0f * s };
        st.ItemInnerSpacing  = {  5.0f * s,  5.0f * s };
        st.CellPadding       = {  5.0f * s,  3.0f * s };
        st.IndentSpacing     = 22.0f * s;
        st.FrameBorderSize   = 1.0f;
        st.TabBorderSize     = 1.0f;
        break;

    // ── Material Design 3 ────────────────────────────────────────────────────
    case ThemeType::Material:
    case ThemeType::MaterialDark:
        st.WindowRounding    = 8.0f * s;
        st.FrameRounding     = 6.0f * s;
        st.ChildRounding     = 6.0f * s;
        st.PopupRounding     = 8.0f * s;
        st.TabRounding       = 6.0f * s;
        st.ScrollbarRounding = 8.0f * s;
        st.GrabRounding      = 6.0f * s;
        st.ScrollbarSize     = 14.0f * s;
        st.GrabMinSize       = 10.0f * s;
        st.WindowPadding     = { 12.0f * s, 12.0f * s };
        st.FramePadding      = {  8.0f * s,  5.0f * s };
        st.ItemSpacing       = {  8.0f * s,  5.0f * s };
        st.ItemInnerSpacing  = {  5.0f * s,  5.0f * s };
        st.CellPadding       = {  5.0f * s,  3.0f * s };
        st.IndentSpacing     = 22.0f * s;
        st.FrameBorderSize   = 0.0f;
        st.WindowBorderSize  = 0.0f;
        break;

    // ── Neon / Cyberpunk ─────────────────────────────────────────────────────
    case ThemeType::NeonSpaces:
        st.WindowRounding    = 2.0f * s;
        st.FrameRounding     = 2.0f * s;
        st.ChildRounding     = 2.0f * s;
        st.PopupRounding     = 2.0f * s;
        st.TabRounding       = 2.0f * s;
        st.ScrollbarRounding = 2.0f * s;
        st.GrabRounding      = 0.0f;
        st.ScrollbarSize     = 12.0f * s;
        st.GrabMinSize       =  8.0f * s;
        st.WindowPadding     = { 10.0f * s, 10.0f * s };
        st.FramePadding      = {  6.0f * s,  4.0f * s };
        st.ItemSpacing       = {  8.0f * s,  4.0f * s };
        st.ItemInnerSpacing  = {  4.0f * s,  4.0f * s };
        st.CellPadding       = {  4.0f * s,  2.0f * s };
        st.IndentSpacing     = 20.0f * s;
        st.FrameBorderSize   = 1.0f;
        break;

    // ── Earth / SUSE ─────────────────────────────────────────────────────────
    case ThemeType::EarthSUSE:
    case ThemeType::EarthSUSEDark:
        st.WindowRounding    = 5.0f * s;
        st.FrameRounding     = 5.0f * s;
        st.ChildRounding     = 5.0f * s;
        st.PopupRounding     = 5.0f * s;
        st.TabRounding       = 5.0f * s;
        st.ScrollbarRounding = 5.0f * s;
        st.GrabRounding      = 5.0f * s;
        st.ScrollbarSize     = 14.0f * s;
        st.GrabMinSize       = 10.0f * s;
        st.WindowPadding     = { 10.0f * s, 10.0f * s };
        st.FramePadding      = {  6.0f * s,  4.0f * s };
        st.ItemSpacing       = {  8.0f * s,  4.0f * s };
        st.ItemInnerSpacing  = {  4.0f * s,  4.0f * s };
        st.CellPadding       = {  4.0f * s,  2.0f * s };
        st.IndentSpacing     = 21.0f * s;
        break;

    // ── Minimal Mono ─────────────────────────────────────────────────────────
    case ThemeType::MonoLight:
    case ThemeType::MonoDark:
        st.WindowRounding    = 2.0f * s;
        st.FrameRounding     = 2.0f * s;
        st.ChildRounding     = 2.0f * s;
        st.PopupRounding     = 2.0f * s;
        st.TabRounding       = 2.0f * s;
        st.ScrollbarRounding = 2.0f * s;
        st.GrabRounding      = 2.0f * s;
        st.ScrollbarSize     = 12.0f * s;
        st.GrabMinSize       =  8.0f * s;
        st.WindowPadding     = {  8.0f * s,  8.0f * s };
        st.FramePadding      = {  4.0f * s,  3.0f * s };
        st.ItemSpacing       = {  6.0f * s,  3.0f * s };
        st.ItemInnerSpacing  = {  3.0f * s,  3.0f * s };
        st.CellPadding       = {  3.0f * s,  2.0f * s };
        st.IndentSpacing     = 18.0f * s;
        st.FrameBorderSize   = 1.0f;
        break;

    // ── Classic (Solarized, Monokai, default) ────────────────────────────────
    default:
        st.WindowRounding    = 4.0f * s;
        st.FrameRounding     = 4.0f * s;
        st.ChildRounding     = 4.0f * s;
        st.PopupRounding     = 4.0f * s;
        st.TabRounding       = 4.0f * s;
        st.ScrollbarRounding = 4.0f * s;
        st.GrabRounding      = 4.0f * s;
        st.ScrollbarSize     = 14.0f * s;
        st.GrabMinSize       = 10.0f * s;
        st.WindowPadding     = { 10.0f * s, 10.0f * s };
        st.FramePadding      = {  6.0f * s,  4.0f * s };
        st.ItemSpacing       = {  8.0f * s,  4.0f * s };
        st.ItemInnerSpacing  = {  4.0f * s,  4.0f * s };
        st.CellPadding       = {  4.0f * s,  2.0f * s };
        st.IndentSpacing     = 21.0f * s;
        break;
    }
}


// ─── RegisterFonts ───────────────────────────────────────────────────────────
void Theme::RegisterFonts(ThemeType t, ImFont* uiFont, ImFont* monoFont)
{
    const auto i        = static_cast<std::size_t>(t);
    uiFonts_[i]   = uiFont;
    codeFonts_[i] = monoFont;
}

// ─── ApplyColorTheme ──────────────────────────────────────────────────────────
void Theme::ApplyColorTheme(ThemeType theme)
{
    ImVec4* colors = ImGui::GetStyle().Colors;

    switch (theme) {
        // ── Solarized Dark ────────────────────────────────────────────────────
        case ThemeType::SolarizedDark: {
            colors[ImGuiCol_Text]                 = ImVec4(0.93f, 0.91f, 0.84f, 1.00f);
            colors[ImGuiCol_TextDisabled]         = ImVec4(0.40f, 0.55f, 0.56f, 1.00f);
            colors[ImGuiCol_WindowBg]             = ImVec4(0.00f, 0.17f, 0.21f, 1.00f);
            colors[ImGuiCol_ChildBg]              = ImVec4(0.02f, 0.22f, 0.25f, 0.95f);
            colors[ImGuiCol_PopupBg]              = ImVec4(0.00f, 0.15f, 0.19f, 0.98f);
            colors[ImGuiCol_Border]               = ImVec4(0.08f, 0.30f, 0.35f, 1.00f);
            colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            colors[ImGuiCol_FrameBg]              = ImVec4(0.02f, 0.25f, 0.30f, 1.00f);
            colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.18f, 0.38f, 0.38f, 1.00f);
            colors[ImGuiCol_FrameBgActive]        = ImVec4(0.28f, 0.54f, 0.55f, 1.00f);
            colors[ImGuiCol_TitleBg]              = ImVec4(0.00f, 0.15f, 0.18f, 1.00f);
            colors[ImGuiCol_TitleBgActive]        = ImVec4(0.00f, 0.20f, 0.23f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.00f, 0.12f, 0.15f, 1.00f);
            colors[ImGuiCol_MenuBarBg]            = ImVec4(0.00f, 0.14f, 0.17f, 1.00f);
            colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.00f, 0.14f, 0.17f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.10f, 0.30f, 0.35f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.18f, 0.40f, 0.45f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.25f, 0.52f, 0.57f, 1.00f);
            colors[ImGuiCol_CheckMark]            = ImVec4(0.42f, 0.80f, 0.82f, 1.00f);
            colors[ImGuiCol_SliderGrab]           = ImVec4(0.25f, 0.55f, 0.65f, 1.00f);
            colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.35f, 0.68f, 0.78f, 1.00f);
            colors[ImGuiCol_Button]               = ImVec4(0.11f, 0.28f, 0.32f, 1.00f);
            colors[ImGuiCol_ButtonHovered]        = ImVec4(0.15f, 0.38f, 0.40f, 1.00f);
            colors[ImGuiCol_ButtonActive]         = ImVec4(0.18f, 0.45f, 0.48f, 1.00f);
            colors[ImGuiCol_Header]               = colors[ImGuiCol_Button];
            colors[ImGuiCol_HeaderHovered]        = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_HeaderActive]         = colors[ImGuiCol_ButtonActive];
            colors[ImGuiCol_Separator]            = ImVec4(0.08f, 0.30f, 0.35f, 1.00f);
            colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.18f, 0.42f, 0.48f, 1.00f);
            colors[ImGuiCol_SeparatorActive]      = ImVec4(0.28f, 0.55f, 0.60f, 1.00f);
            colors[ImGuiCol_ResizeGrip]           = ImVec4(0.10f, 0.28f, 0.32f, 0.50f);
            colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.20f, 0.42f, 0.46f, 0.80f);
            colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.30f, 0.55f, 0.60f, 1.00f);
            colors[ImGuiCol_Tab]                  = colors[ImGuiCol_Button];
            colors[ImGuiCol_TabHovered]           = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_TabSelected]          = colors[ImGuiCol_ButtonActive];
            colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.00f, 0.18f, 0.22f, 1.00f);
            colors[ImGuiCol_TableBorderStrong]    = ImVec4(0.08f, 0.30f, 0.35f, 1.00f);
            colors[ImGuiCol_TableBorderLight]     = ImVec4(0.05f, 0.22f, 0.26f, 1.00f);
            colors[ImGuiCol_TableRowBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt]        = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
            colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.18f, 0.52f, 0.58f, 0.45f);
            break;
        }
        // ── Solarized Light ───────────────────────────────────────────────────
        case ThemeType::SolarizedLight: {
            colors[ImGuiCol_Text]                 = ImVec4(0.00f, 0.17f, 0.21f, 1.00f);
            colors[ImGuiCol_TextDisabled]         = ImVec4(0.45f, 0.55f, 0.57f, 1.00f);
            colors[ImGuiCol_WindowBg]             = ImVec4(0.99f, 0.96f, 0.89f, 1.00f);
            colors[ImGuiCol_ChildBg]              = ImVec4(0.94f, 0.91f, 0.85f, 1.00f);
            colors[ImGuiCol_PopupBg]              = ImVec4(0.99f, 0.97f, 0.92f, 0.98f);
            colors[ImGuiCol_Border]               = ImVec4(0.72f, 0.72f, 0.68f, 1.00f);
            colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            colors[ImGuiCol_FrameBg]              = ImVec4(0.87f, 0.84f, 0.78f, 1.00f);
            colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.72f, 0.76f, 0.76f, 1.00f);
            colors[ImGuiCol_FrameBgActive]        = ImVec4(0.60f, 0.60f, 0.58f, 1.00f);
            colors[ImGuiCol_TitleBg]              = ImVec4(0.89f, 0.87f, 0.83f, 1.00f);
            colors[ImGuiCol_TitleBgActive]        = ImVec4(0.78f, 0.76f, 0.70f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.92f, 0.90f, 0.86f, 1.00f);
            colors[ImGuiCol_MenuBarBg]            = ImVec4(0.91f, 0.89f, 0.84f, 1.00f);
            colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.91f, 0.89f, 0.84f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.70f, 0.68f, 0.63f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.60f, 0.58f, 0.54f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.50f, 0.48f, 0.44f, 1.00f);
            colors[ImGuiCol_CheckMark]            = ImVec4(0.20f, 0.52f, 0.53f, 1.00f);
            colors[ImGuiCol_SliderGrab]           = ImVec4(0.35f, 0.60f, 0.62f, 1.00f);
            colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.20f, 0.48f, 0.50f, 1.00f);
            colors[ImGuiCol_Button]               = ImVec4(0.75f, 0.65f, 0.53f, 1.00f);
            colors[ImGuiCol_ButtonHovered]        = ImVec4(0.83f, 0.73f, 0.62f, 1.00f);
            colors[ImGuiCol_ButtonActive]         = ImVec4(0.93f, 0.80f, 0.60f, 1.00f);
            colors[ImGuiCol_Header]               = colors[ImGuiCol_Button];
            colors[ImGuiCol_HeaderHovered]        = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_HeaderActive]         = colors[ImGuiCol_ButtonActive];
            colors[ImGuiCol_Separator]            = ImVec4(0.72f, 0.72f, 0.68f, 1.00f);
            colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.55f, 0.55f, 0.50f, 1.00f);
            colors[ImGuiCol_SeparatorActive]      = ImVec4(0.40f, 0.40f, 0.36f, 1.00f);
            colors[ImGuiCol_ResizeGrip]           = ImVec4(0.70f, 0.65f, 0.53f, 0.50f);
            colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.60f, 0.55f, 0.45f, 0.80f);
            colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.50f, 0.45f, 0.36f, 1.00f);
            colors[ImGuiCol_Tab]                  = colors[ImGuiCol_Button];
            colors[ImGuiCol_TabHovered]           = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_TabSelected]          = colors[ImGuiCol_ButtonActive];
            colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.84f, 0.82f, 0.77f, 1.00f);
            colors[ImGuiCol_TableBorderStrong]    = ImVec4(0.68f, 0.66f, 0.60f, 1.00f);
            colors[ImGuiCol_TableBorderLight]     = ImVec4(0.78f, 0.76f, 0.70f, 1.00f);
            colors[ImGuiCol_TableRowBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt]        = ImVec4(0.00f, 0.00f, 0.00f, 0.04f);
            colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.55f, 0.45f, 0.20f, 0.40f);
            break;
        }
        // ── Monokai ───────────────────────────────────────────────────────────
        case ThemeType::Monokai: {
            colors[ImGuiCol_Text]                 = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
            colors[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
            colors[ImGuiCol_WindowBg]             = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
            colors[ImGuiCol_ChildBg]              = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
            colors[ImGuiCol_PopupBg]              = ImVec4(0.08f, 0.08f, 0.08f, 0.98f);
            colors[ImGuiCol_Border]               = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
            colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            colors[ImGuiCol_FrameBg]              = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
            colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
            colors[ImGuiCol_FrameBgActive]        = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
            colors[ImGuiCol_TitleBg]              = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
            colors[ImGuiCol_TitleBgActive]        = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);
            colors[ImGuiCol_MenuBarBg]            = ImVec4(0.09f, 0.09f, 0.09f, 1.00f);
            colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.32f, 0.32f, 0.32f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
            colors[ImGuiCol_CheckMark]            = ImVec4(0.64f, 0.87f, 0.29f, 1.00f);
            colors[ImGuiCol_SliderGrab]           = ImVec4(0.44f, 0.33f, 0.53f, 1.00f);
            colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.56f, 0.44f, 0.66f, 1.00f);
            colors[ImGuiCol_Button]               = ImVec4(0.44f, 0.33f, 0.53f, 1.00f);
            colors[ImGuiCol_ButtonHovered]        = ImVec4(0.54f, 0.43f, 0.63f, 1.00f);
            colors[ImGuiCol_ButtonActive]         = ImVec4(0.64f, 0.53f, 0.73f, 1.00f);
            colors[ImGuiCol_Header]               = ImVec4(0.27f, 0.36f, 0.43f, 1.00f);
            colors[ImGuiCol_HeaderHovered]        = ImVec4(0.34f, 0.44f, 0.52f, 1.00f);
            colors[ImGuiCol_HeaderActive]         = ImVec4(0.42f, 0.53f, 0.62f, 1.00f);
            colors[ImGuiCol_Separator]            = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
            colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.34f, 0.44f, 0.52f, 1.00f);
            colors[ImGuiCol_SeparatorActive]      = ImVec4(0.42f, 0.53f, 0.62f, 1.00f);
            colors[ImGuiCol_ResizeGrip]           = ImVec4(0.44f, 0.33f, 0.53f, 0.40f);
            colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.54f, 0.43f, 0.63f, 0.70f);
            colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.64f, 0.53f, 0.73f, 1.00f);
            colors[ImGuiCol_Tab]                  = colors[ImGuiCol_Button];
            colors[ImGuiCol_TabHovered]           = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_TabSelected]          = colors[ImGuiCol_ButtonActive];
            colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
            colors[ImGuiCol_TableBorderStrong]    = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
            colors[ImGuiCol_TableBorderLight]     = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
            colors[ImGuiCol_TableRowBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt]        = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
            colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.44f, 0.33f, 0.53f, 0.45f);
            break;
        }
        // ── EarthSUSE — warm chocolate + SUSE green ───────────────────────────
        // UI font: SUSE-Light   Code font: SUSEMono-Light
        case ThemeType::EarthSUSE: {
            // Background palette
            // bg0  #231C13  — dark chocolate
            // bg1  #2E2519  — warm dark brown (ChildBg/FrameBg)
            // bg2  #3D3120  — mid brown (hover)
            // fg0  #EDE8D9  — warm ivory
            // fg1  #BDB0A0  — muted tan (TextDisabled)
            // brd  #5A4B36  — dark tan border
            // acc  #30BA78  — SUSE green
            // acc2 #1A7D4A  — darker SUSE green (pressed)
            colors[ImGuiCol_Text]                 = ImVec4(0.929f, 0.910f, 0.851f, 1.00f);
            colors[ImGuiCol_TextDisabled]         = ImVec4(0.741f, 0.690f, 0.627f, 1.00f);
            colors[ImGuiCol_WindowBg]             = ImVec4(0.137f, 0.110f, 0.075f, 1.00f);
            colors[ImGuiCol_ChildBg]              = ImVec4(0.180f, 0.145f, 0.098f, 1.00f);
            colors[ImGuiCol_PopupBg]              = ImVec4(0.118f, 0.094f, 0.063f, 0.98f);
            colors[ImGuiCol_Border]               = ImVec4(0.353f, 0.294f, 0.212f, 1.00f);
            colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            colors[ImGuiCol_FrameBg]              = ImVec4(0.204f, 0.165f, 0.110f, 1.00f);
            colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.278f, 0.224f, 0.153f, 1.00f);
            colors[ImGuiCol_FrameBgActive]        = ImVec4(0.353f, 0.286f, 0.196f, 1.00f);
            colors[ImGuiCol_TitleBg]              = ImVec4(0.098f, 0.078f, 0.051f, 1.00f);
            colors[ImGuiCol_TitleBgActive]        = ImVec4(0.137f, 0.110f, 0.075f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.078f, 0.063f, 0.039f, 1.00f);
            colors[ImGuiCol_MenuBarBg]            = ImVec4(0.098f, 0.078f, 0.051f, 1.00f);
            colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.098f, 0.078f, 0.051f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.353f, 0.294f, 0.212f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.467f, 0.392f, 0.282f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.557f, 0.467f, 0.337f, 1.00f);
            // SUSE green accent
            colors[ImGuiCol_CheckMark]            = ImVec4(0.188f, 0.729f, 0.471f, 1.00f);
            colors[ImGuiCol_SliderGrab]           = ImVec4(0.188f, 0.580f, 0.380f, 1.00f);
            colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.188f, 0.729f, 0.471f, 1.00f);
            colors[ImGuiCol_Button]               = ImVec4(0.102f, 0.361f, 0.239f, 1.00f);
            colors[ImGuiCol_ButtonHovered]        = ImVec4(0.145f, 0.478f, 0.314f, 1.00f);
            colors[ImGuiCol_ButtonActive]         = ImVec4(0.188f, 0.580f, 0.380f, 1.00f);
            colors[ImGuiCol_Header]               = ImVec4(0.118f, 0.298f, 0.200f, 1.00f);
            colors[ImGuiCol_HeaderHovered]        = ImVec4(0.145f, 0.416f, 0.275f, 1.00f);
            colors[ImGuiCol_HeaderActive]         = ImVec4(0.188f, 0.510f, 0.333f, 1.00f);
            colors[ImGuiCol_Separator]            = ImVec4(0.353f, 0.294f, 0.212f, 1.00f);
            colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.188f, 0.580f, 0.380f, 0.80f);
            colors[ImGuiCol_SeparatorActive]      = ImVec4(0.188f, 0.729f, 0.471f, 1.00f);
            colors[ImGuiCol_ResizeGrip]           = ImVec4(0.102f, 0.361f, 0.239f, 0.40f);
            colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.145f, 0.478f, 0.314f, 0.75f);
            colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.188f, 0.580f, 0.380f, 1.00f);
            colors[ImGuiCol_Tab]                  = colors[ImGuiCol_Button];
            colors[ImGuiCol_TabHovered]           = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_TabSelected]          = colors[ImGuiCol_ButtonActive];
            colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.157f, 0.126f, 0.086f, 1.00f);
            colors[ImGuiCol_TableBorderStrong]    = ImVec4(0.353f, 0.294f, 0.212f, 1.00f);
            colors[ImGuiCol_TableBorderLight]     = ImVec4(0.255f, 0.204f, 0.141f, 1.00f);
            colors[ImGuiCol_TableRowBg]           = ImVec4(0.00f,  0.00f,  0.00f,  0.00f);
            colors[ImGuiCol_TableRowBgAlt]        = ImVec4(1.00f,  1.00f,  1.00f,  0.03f);
            colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.188f, 0.729f, 0.471f, 0.38f);
            break;
        }
        // ── NeonSpaces — electric neon on deep purple-black ──────────────────
        // UI font: Roboto-Medium   Code font: MonaspaceArgonNF-Regular
        case ThemeType::NeonSpaces: {
            // bg0  #07021A  — deep purple-black
            // bg1  #0B0528  — slightly lighter (ChildBg)
            // bg2  #130B38  — mid purple (FrameBg)
            // fg0  #E8F4FF  — cool white
            // fg1  #5566AA  — muted blue-grey (TextDisabled)
            // brd  #300070  — electric purple border
            // acc  #7B00FF  — neon electric purple
            // neon #00F5FF  — neon cyan
            colors[ImGuiCol_Text]                 = ImVec4(0.910f, 0.957f, 1.000f, 1.00f);
            colors[ImGuiCol_TextDisabled]         = ImVec4(0.333f, 0.400f, 0.667f, 1.00f);
            colors[ImGuiCol_WindowBg]             = ImVec4(0.027f, 0.008f, 0.102f, 1.00f);
            colors[ImGuiCol_ChildBg]              = ImVec4(0.043f, 0.020f, 0.157f, 1.00f);
            colors[ImGuiCol_PopupBg]              = ImVec4(0.020f, 0.004f, 0.082f, 0.98f);
            colors[ImGuiCol_Border]               = ImVec4(0.471f, 0.000f, 0.882f, 0.70f);
            colors[ImGuiCol_BorderShadow]         = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_FrameBg]              = ImVec4(0.075f, 0.039f, 0.196f, 1.00f);
            colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.118f, 0.059f, 0.278f, 1.00f);
            colors[ImGuiCol_FrameBgActive]        = ImVec4(0.165f, 0.082f, 0.365f, 1.00f);
            colors[ImGuiCol_TitleBg]              = ImVec4(0.039f, 0.012f, 0.141f, 1.00f);
            colors[ImGuiCol_TitleBgActive]        = ImVec4(0.063f, 0.024f, 0.196f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.020f, 0.004f, 0.082f, 1.00f);
            colors[ImGuiCol_MenuBarBg]            = ImVec4(0.027f, 0.008f, 0.118f, 1.00f);
            colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.020f, 0.004f, 0.082f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.247f, 0.000f, 0.502f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.369f, 0.008f, 0.706f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.482f, 0.016f, 0.906f, 1.00f);
            // neon cyan checkmark / slider
            colors[ImGuiCol_CheckMark]            = ImVec4(0.000f, 0.961f, 1.000f, 1.00f);
            colors[ImGuiCol_SliderGrab]           = ImVec4(0.400f, 0.000f, 0.800f, 1.00f);
            colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.000f, 0.961f, 1.000f, 1.00f);
            // electric purple buttons
            colors[ImGuiCol_Button]               = ImVec4(0.310f, 0.000f, 0.620f, 1.00f);
            colors[ImGuiCol_ButtonHovered]        = ImVec4(0.427f, 0.016f, 0.800f, 1.00f);
            colors[ImGuiCol_ButtonActive]         = ImVec4(0.565f, 0.031f, 0.949f, 1.00f);
            colors[ImGuiCol_Header]               = ImVec4(0.180f, 0.000f, 0.420f, 1.00f);
            colors[ImGuiCol_HeaderHovered]        = ImVec4(0.290f, 0.016f, 0.620f, 1.00f);
            colors[ImGuiCol_HeaderActive]         = ImVec4(0.400f, 0.031f, 0.780f, 1.00f);
            colors[ImGuiCol_Separator]            = ImVec4(0.247f, 0.000f, 0.502f, 1.00f);
            colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.000f, 0.961f, 1.000f, 0.70f);
            colors[ImGuiCol_SeparatorActive]      = ImVec4(0.000f, 0.961f, 1.000f, 1.00f);
            colors[ImGuiCol_ResizeGrip]           = ImVec4(0.310f, 0.000f, 0.620f, 0.40f);
            colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.427f, 0.016f, 0.800f, 0.75f);
            colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.000f, 0.961f, 1.000f, 1.00f);
            colors[ImGuiCol_Tab]                  = colors[ImGuiCol_Button];
            colors[ImGuiCol_TabHovered]           = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_TabSelected]          = colors[ImGuiCol_ButtonActive];
            colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.055f, 0.016f, 0.157f, 1.00f);
            colors[ImGuiCol_TableBorderStrong]    = ImVec4(0.247f, 0.000f, 0.502f, 1.00f);
            colors[ImGuiCol_TableBorderLight]     = ImVec4(0.118f, 0.008f, 0.275f, 1.00f);
            colors[ImGuiCol_TableRowBg]           = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt]        = ImVec4(1.000f, 1.000f, 1.000f, 0.03f);
            colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.000f, 0.961f, 1.000f, 0.30f);
            break;
        }
        // ── DawnBringer 16 — Dark ─────────────────────────────────────────────
        // Palette: Black/#140C1C  Dark Blue/#30346D  Cyan/#6DC2CA  White/#DEEED6
        // UI font: Hack-Regular   Code font: Hack-Regular
        case ThemeType::DawnBringer16Dark: {
            // DB16 colour shortcuts (float)
            //  blk  (0.078, 0.047, 0.110)   #140C1C
            //  dred (0.267, 0.141, 0.204)   #442434
            //  dblu (0.188, 0.204, 0.427)   #30346D
            //  dgry (0.306, 0.290, 0.306)   #4E4A4F
            //  lblu (0.349, 0.490, 0.808)   #597DCE
            //  cyan (0.427, 0.761, 0.792)   #6DC2CA
            //  lgry (0.459, 0.443, 0.380)   #757161
            //  wht  (0.871, 0.933, 0.839)   #DEEED6
            colors[ImGuiCol_Text]                 = ImVec4(0.871f, 0.933f, 0.839f, 1.00f);
            colors[ImGuiCol_TextDisabled]         = ImVec4(0.459f, 0.443f, 0.380f, 1.00f);
            colors[ImGuiCol_WindowBg]             = ImVec4(0.078f, 0.047f, 0.110f, 1.00f);
            colors[ImGuiCol_ChildBg]              = ImVec4(0.098f, 0.063f, 0.137f, 1.00f);
            colors[ImGuiCol_PopupBg]              = ImVec4(0.059f, 0.035f, 0.090f, 0.98f);
            colors[ImGuiCol_Border]               = ImVec4(0.306f, 0.290f, 0.306f, 1.00f);
            colors[ImGuiCol_BorderShadow]         = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_FrameBg]              = ImVec4(0.267f, 0.141f, 0.204f, 1.00f);
            colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.188f, 0.204f, 0.427f, 1.00f);
            colors[ImGuiCol_FrameBgActive]        = ImVec4(0.349f, 0.490f, 0.808f, 1.00f);
            colors[ImGuiCol_TitleBg]              = ImVec4(0.090f, 0.055f, 0.129f, 1.00f);
            colors[ImGuiCol_TitleBgActive]        = ImVec4(0.267f, 0.141f, 0.204f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.059f, 0.035f, 0.086f, 1.00f);
            colors[ImGuiCol_MenuBarBg]            = ImVec4(0.090f, 0.055f, 0.129f, 1.00f);
            colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.078f, 0.047f, 0.110f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.306f, 0.290f, 0.306f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.427f, 0.761f, 0.792f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.349f, 0.490f, 0.808f, 1.00f);
            colors[ImGuiCol_CheckMark]            = ImVec4(0.427f, 0.761f, 0.792f, 1.00f);
            colors[ImGuiCol_SliderGrab]           = ImVec4(0.188f, 0.204f, 0.427f, 1.00f);
            colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.427f, 0.761f, 0.792f, 1.00f);
            colors[ImGuiCol_Button]               = ImVec4(0.188f, 0.204f, 0.427f, 1.00f);
            colors[ImGuiCol_ButtonHovered]        = ImVec4(0.280f, 0.310f, 0.580f, 1.00f);
            colors[ImGuiCol_ButtonActive]         = ImVec4(0.427f, 0.761f, 0.792f, 1.00f);
            colors[ImGuiCol_Header]               = ImVec4(0.267f, 0.141f, 0.204f, 1.00f);
            colors[ImGuiCol_HeaderHovered]        = ImVec4(0.188f, 0.204f, 0.427f, 1.00f);
            colors[ImGuiCol_HeaderActive]         = ImVec4(0.349f, 0.490f, 0.808f, 1.00f);
            colors[ImGuiCol_Separator]            = ImVec4(0.306f, 0.290f, 0.306f, 1.00f);
            colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.427f, 0.761f, 0.792f, 0.80f);
            colors[ImGuiCol_SeparatorActive]      = ImVec4(0.427f, 0.761f, 0.792f, 1.00f);
            colors[ImGuiCol_ResizeGrip]           = ImVec4(0.188f, 0.204f, 0.427f, 0.40f);
            colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.280f, 0.310f, 0.580f, 0.75f);
            colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.427f, 0.761f, 0.792f, 1.00f);
            colors[ImGuiCol_Tab]                  = colors[ImGuiCol_Button];
            colors[ImGuiCol_TabHovered]           = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_TabSelected]          = colors[ImGuiCol_ButtonActive];
            colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.267f, 0.141f, 0.204f, 1.00f);
            colors[ImGuiCol_TableBorderStrong]    = ImVec4(0.306f, 0.290f, 0.306f, 1.00f);
            colors[ImGuiCol_TableBorderLight]     = ImVec4(0.196f, 0.184f, 0.200f, 1.00f);
            colors[ImGuiCol_TableRowBg]           = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt]        = ImVec4(1.000f, 1.000f, 1.000f, 0.04f);
            colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.427f, 0.761f, 0.792f, 0.38f);
            break;
        }
        // ── DawnBringer 16 — Light ────────────────────────────────────────────
        // Palette: White/#DEEED6  Peach/#D2AA99  Light Blue/#597DCE  Black/#140C1C
        // UI font: Hack-Regular   Code font: Hack-Regular
        case ThemeType::DawnBringer16Light: {
            colors[ImGuiCol_Text]                 = ImVec4(0.078f, 0.047f, 0.110f, 1.00f);
            colors[ImGuiCol_TextDisabled]         = ImVec4(0.459f, 0.443f, 0.380f, 1.00f);
            colors[ImGuiCol_WindowBg]             = ImVec4(0.871f, 0.933f, 0.839f, 1.00f);
            colors[ImGuiCol_ChildBg]              = ImVec4(0.835f, 0.898f, 0.808f, 1.00f);
            colors[ImGuiCol_PopupBg]              = ImVec4(0.890f, 0.945f, 0.859f, 0.98f);
            colors[ImGuiCol_Border]               = ImVec4(0.522f, 0.584f, 0.631f, 1.00f);
            colors[ImGuiCol_BorderShadow]         = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_FrameBg]              = ImVec4(0.824f, 0.667f, 0.600f, 1.00f);
            colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.710f, 0.745f, 0.855f, 1.00f);
            colors[ImGuiCol_FrameBgActive]        = ImVec4(0.349f, 0.490f, 0.808f, 0.70f);
            colors[ImGuiCol_TitleBg]              = ImVec4(0.820f, 0.882f, 0.800f, 1.00f);
            colors[ImGuiCol_TitleBgActive]        = ImVec4(0.780f, 0.843f, 0.757f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.855f, 0.914f, 0.827f, 1.00f);
            colors[ImGuiCol_MenuBarBg]            = ImVec4(0.820f, 0.882f, 0.800f, 1.00f);
            colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.835f, 0.898f, 0.808f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.522f, 0.584f, 0.631f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.427f, 0.667f, 0.173f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.204f, 0.396f, 0.141f, 1.00f);
            colors[ImGuiCol_CheckMark]            = ImVec4(0.204f, 0.396f, 0.141f, 1.00f);
            colors[ImGuiCol_SliderGrab]           = ImVec4(0.349f, 0.490f, 0.808f, 1.00f);
            colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.204f, 0.396f, 0.141f, 1.00f);
            colors[ImGuiCol_Button]               = ImVec4(0.204f, 0.396f, 0.141f, 1.00f);
            colors[ImGuiCol_ButtonHovered]        = ImVec4(0.310f, 0.537f, 0.224f, 1.00f);
            colors[ImGuiCol_ButtonActive]         = ImVec4(0.427f, 0.667f, 0.173f, 1.00f);
            colors[ImGuiCol_Header]               = ImVec4(0.710f, 0.767f, 0.855f, 1.00f);
            colors[ImGuiCol_HeaderHovered]        = ImVec4(0.349f, 0.490f, 0.808f, 0.70f);
            colors[ImGuiCol_HeaderActive]         = ImVec4(0.188f, 0.310f, 0.620f, 1.00f);
            colors[ImGuiCol_Separator]            = ImVec4(0.522f, 0.584f, 0.631f, 1.00f);
            colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.204f, 0.396f, 0.141f, 0.80f);
            colors[ImGuiCol_SeparatorActive]      = ImVec4(0.204f, 0.396f, 0.141f, 1.00f);
            colors[ImGuiCol_ResizeGrip]           = ImVec4(0.204f, 0.396f, 0.141f, 0.35f);
            colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.310f, 0.537f, 0.224f, 0.70f);
            colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.427f, 0.667f, 0.173f, 1.00f);
            colors[ImGuiCol_Tab]                  = ImVec4(0.824f, 0.710f, 0.655f, 1.00f);
            colors[ImGuiCol_TabHovered]           = ImVec4(0.349f, 0.490f, 0.808f, 0.70f);
            colors[ImGuiCol_TabSelected]          = ImVec4(0.204f, 0.396f, 0.141f, 1.00f);
            colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.824f, 0.667f, 0.600f, 1.00f);
            colors[ImGuiCol_TableBorderStrong]    = ImVec4(0.522f, 0.584f, 0.631f, 1.00f);
            colors[ImGuiCol_TableBorderLight]     = ImVec4(0.700f, 0.757f, 0.700f, 1.00f);
            colors[ImGuiCol_TableRowBg]           = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt]        = ImVec4(0.000f, 0.000f, 0.000f, 0.04f);
            colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.427f, 0.667f, 0.173f, 0.38f);
            break;
        }
        // ── MonokaiDark — Monokai Pro (deeper dark) ───────────────────────────
        // bg: #2D2A2E  fg: #FCFCFA  accent: AB9DF2 purple + A9DC76 green
        // UI font: Roboto-Medium   Code font: Hack-Regular
        case ThemeType::MonokaiDark: {
            colors[ImGuiCol_Text]                 = ImVec4(0.988f, 0.988f, 0.980f, 1.00f);
            colors[ImGuiCol_TextDisabled]         = ImVec4(0.502f, 0.478f, 0.498f, 1.00f);
            colors[ImGuiCol_WindowBg]             = ImVec4(0.176f, 0.165f, 0.180f, 1.00f);
            colors[ImGuiCol_ChildBg]              = ImVec4(0.145f, 0.133f, 0.149f, 1.00f);
            colors[ImGuiCol_PopupBg]              = ImVec4(0.133f, 0.122f, 0.133f, 0.98f);
            colors[ImGuiCol_Border]               = ImVec4(0.239f, 0.224f, 0.243f, 1.00f);
            colors[ImGuiCol_BorderShadow]         = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_FrameBg]              = ImVec4(0.224f, 0.208f, 0.228f, 1.00f);
            colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.271f, 0.255f, 0.275f, 1.00f);
            colors[ImGuiCol_FrameBgActive]        = ImVec4(0.322f, 0.306f, 0.329f, 1.00f);
            colors[ImGuiCol_TitleBg]              = ImVec4(0.133f, 0.122f, 0.133f, 1.00f);
            colors[ImGuiCol_TitleBgActive]        = ImVec4(0.176f, 0.165f, 0.180f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.110f, 0.102f, 0.114f, 1.00f);
            colors[ImGuiCol_MenuBarBg]            = ImVec4(0.141f, 0.133f, 0.145f, 1.00f);
            colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.133f, 0.122f, 0.133f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.271f, 0.255f, 0.275f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.341f, 0.325f, 0.345f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.420f, 0.400f, 0.424f, 1.00f);
            colors[ImGuiCol_CheckMark]            = ImVec4(0.663f, 0.863f, 0.463f, 1.00f); // #A9DC76 green
            colors[ImGuiCol_SliderGrab]           = ImVec4(0.671f, 0.616f, 0.949f, 1.00f); // #AB9DF2 purple
            colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.741f, 0.698f, 0.969f, 1.00f);
            colors[ImGuiCol_Button]               = ImVec4(0.420f, 0.380f, 0.569f, 1.00f);
            colors[ImGuiCol_ButtonHovered]        = ImVec4(0.510f, 0.471f, 0.651f, 1.00f);
            colors[ImGuiCol_ButtonActive]         = ImVec4(0.671f, 0.616f, 0.949f, 1.00f);
            colors[ImGuiCol_Header]               = ImVec4(0.271f, 0.255f, 0.365f, 1.00f);
            colors[ImGuiCol_HeaderHovered]        = ImVec4(0.341f, 0.325f, 0.451f, 1.00f);
            colors[ImGuiCol_HeaderActive]         = ImVec4(0.420f, 0.400f, 0.549f, 1.00f);
            colors[ImGuiCol_Separator]            = ImVec4(0.239f, 0.224f, 0.243f, 1.00f);
            colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.671f, 0.616f, 0.949f, 0.80f);
            colors[ImGuiCol_SeparatorActive]      = ImVec4(0.671f, 0.616f, 0.949f, 1.00f);
            colors[ImGuiCol_ResizeGrip]           = ImVec4(0.420f, 0.380f, 0.569f, 0.40f);
            colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.510f, 0.471f, 0.651f, 0.75f);
            colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.671f, 0.616f, 0.949f, 1.00f);
            colors[ImGuiCol_Tab]                  = colors[ImGuiCol_Button];
            colors[ImGuiCol_TabHovered]           = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_TabSelected]          = colors[ImGuiCol_ButtonActive];
            colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.200f, 0.188f, 0.204f, 1.00f);
            colors[ImGuiCol_TableBorderStrong]    = ImVec4(0.239f, 0.224f, 0.243f, 1.00f);
            colors[ImGuiCol_TableBorderLight]     = ImVec4(0.208f, 0.196f, 0.212f, 1.00f);
            colors[ImGuiCol_TableRowBg]           = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt]        = ImVec4(1.000f, 1.000f, 1.000f, 0.03f);
            colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.671f, 0.616f, 0.949f, 0.35f);
            break;
        }
        // ── MonaSpaces — botanical/floral light + Karla + MonaspaceKrypton ────
        // bg: #F5EFE7 ivory  accent: dusty rose + sage green + lavender
        // UI font: Karla-Regular   Code font: MonaspaceKryptonNF-WideLight
        case ThemeType::MonaSpaces: {
            colors[ImGuiCol_Text]                 = ImVec4(0.169f, 0.118f, 0.078f, 1.00f);
            colors[ImGuiCol_TextDisabled]         = ImVec4(0.549f, 0.478f, 0.427f, 1.00f);
            colors[ImGuiCol_WindowBg]             = ImVec4(0.961f, 0.937f, 0.906f, 1.00f);
            colors[ImGuiCol_ChildBg]              = ImVec4(0.945f, 0.918f, 0.882f, 1.00f);
            colors[ImGuiCol_PopupBg]              = ImVec4(0.973f, 0.953f, 0.925f, 0.98f);
            colors[ImGuiCol_Border]               = ImVec4(0.741f, 0.690f, 0.639f, 1.00f);
            colors[ImGuiCol_BorderShadow]         = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_FrameBg]              = ImVec4(0.882f, 0.847f, 0.808f, 1.00f);
            colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.827f, 0.784f, 0.741f, 1.00f);
            colors[ImGuiCol_FrameBgActive]        = ImVec4(0.773f, 0.722f, 0.675f, 1.00f);
            colors[ImGuiCol_TitleBg]              = ImVec4(0.929f, 0.902f, 0.867f, 1.00f);
            colors[ImGuiCol_TitleBgActive]        = ImVec4(0.898f, 0.863f, 0.820f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.953f, 0.933f, 0.902f, 1.00f);
            colors[ImGuiCol_MenuBarBg]            = ImVec4(0.929f, 0.902f, 0.867f, 1.00f);
            colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.929f, 0.902f, 0.867f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.741f, 0.690f, 0.639f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.639f, 0.588f, 0.541f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.549f, 0.498f, 0.455f, 1.00f);
            colors[ImGuiCol_CheckMark]            = ImVec4(0.420f, 0.608f, 0.478f, 1.00f); // sage
            colors[ImGuiCol_SliderGrab]           = ImVec4(0.608f, 0.557f, 0.769f, 1.00f); // lavender
            colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.769f, 0.482f, 0.478f, 1.00f); // rose
            colors[ImGuiCol_Button]               = ImVec4(0.769f, 0.482f, 0.478f, 1.00f); // dusty rose
            colors[ImGuiCol_ButtonHovered]        = ImVec4(0.820f, 0.557f, 0.553f, 1.00f);
            colors[ImGuiCol_ButtonActive]         = ImVec4(0.863f, 0.627f, 0.624f, 1.00f);
            colors[ImGuiCol_Header]               = ImVec4(0.820f, 0.776f, 0.745f, 1.00f);
            colors[ImGuiCol_HeaderHovered]        = ImVec4(0.769f, 0.482f, 0.478f, 0.70f);
            colors[ImGuiCol_HeaderActive]         = ImVec4(0.769f, 0.482f, 0.478f, 1.00f);
            colors[ImGuiCol_Separator]            = ImVec4(0.741f, 0.690f, 0.639f, 1.00f);
            colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.420f, 0.608f, 0.478f, 0.80f);
            colors[ImGuiCol_SeparatorActive]      = ImVec4(0.420f, 0.608f, 0.478f, 1.00f);
            colors[ImGuiCol_ResizeGrip]           = ImVec4(0.769f, 0.482f, 0.478f, 0.35f);
            colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.820f, 0.557f, 0.553f, 0.70f);
            colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.863f, 0.627f, 0.624f, 1.00f);
            colors[ImGuiCol_Tab]                  = colors[ImGuiCol_Button];
            colors[ImGuiCol_TabHovered]           = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_TabSelected]          = colors[ImGuiCol_ButtonActive];
            colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.878f, 0.839f, 0.800f, 1.00f);
            colors[ImGuiCol_TableBorderStrong]    = ImVec4(0.741f, 0.690f, 0.639f, 1.00f);
            colors[ImGuiCol_TableBorderLight]     = ImVec4(0.820f, 0.776f, 0.733f, 1.00f);
            colors[ImGuiCol_TableRowBg]           = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt]        = ImVec4(0.000f, 0.000f, 0.000f, 0.04f);
            colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.769f, 0.482f, 0.478f, 0.35f);
            break;
        }
        // ── EarthSUSEDark — near-black chocolate + SUSE green ─────────────────
        // Darker variant of EarthSUSE; same SUSE green accent, deeper background.
        // UI font: SUSE-Light   Code font: SUSEMono-Light
        case ThemeType::EarthSUSEDark: {
            colors[ImGuiCol_Text]                 = ImVec4(0.929f, 0.910f, 0.851f, 1.00f);
            colors[ImGuiCol_TextDisabled]         = ImVec4(0.600f, 0.549f, 0.490f, 1.00f);
            colors[ImGuiCol_WindowBg]             = ImVec4(0.059f, 0.039f, 0.020f, 1.00f);
            colors[ImGuiCol_ChildBg]              = ImVec4(0.078f, 0.055f, 0.031f, 1.00f);
            colors[ImGuiCol_PopupBg]              = ImVec4(0.047f, 0.031f, 0.016f, 0.98f);
            colors[ImGuiCol_Border]               = ImVec4(0.255f, 0.196f, 0.133f, 1.00f);
            colors[ImGuiCol_BorderShadow]         = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_FrameBg]              = ImVec4(0.118f, 0.086f, 0.051f, 1.00f);
            colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.196f, 0.145f, 0.086f, 1.00f);
            colors[ImGuiCol_FrameBgActive]        = ImVec4(0.275f, 0.204f, 0.122f, 1.00f);
            colors[ImGuiCol_TitleBg]              = ImVec4(0.039f, 0.027f, 0.012f, 1.00f);
            colors[ImGuiCol_TitleBgActive]        = ImVec4(0.059f, 0.039f, 0.020f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.027f, 0.016f, 0.008f, 1.00f);
            colors[ImGuiCol_MenuBarBg]            = ImVec4(0.039f, 0.027f, 0.012f, 1.00f);
            colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.039f, 0.027f, 0.012f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.255f, 0.196f, 0.133f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.353f, 0.275f, 0.188f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.451f, 0.353f, 0.243f, 1.00f);
            colors[ImGuiCol_CheckMark]            = ImVec4(0.188f, 0.729f, 0.471f, 1.00f);
            colors[ImGuiCol_SliderGrab]           = ImVec4(0.102f, 0.451f, 0.290f, 1.00f);
            colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.188f, 0.729f, 0.471f, 1.00f);
            colors[ImGuiCol_Button]               = ImVec4(0.063f, 0.251f, 0.161f, 1.00f);
            colors[ImGuiCol_ButtonHovered]        = ImVec4(0.102f, 0.380f, 0.243f, 1.00f);
            colors[ImGuiCol_ButtonActive]         = ImVec4(0.145f, 0.502f, 0.318f, 1.00f);
            colors[ImGuiCol_Header]               = ImVec4(0.078f, 0.220f, 0.145f, 1.00f);
            colors[ImGuiCol_HeaderHovered]        = ImVec4(0.102f, 0.322f, 0.208f, 1.00f);
            colors[ImGuiCol_HeaderActive]         = ImVec4(0.145f, 0.431f, 0.275f, 1.00f);
            colors[ImGuiCol_Separator]            = ImVec4(0.255f, 0.196f, 0.133f, 1.00f);
            colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.145f, 0.502f, 0.318f, 0.80f);
            colors[ImGuiCol_SeparatorActive]      = ImVec4(0.188f, 0.729f, 0.471f, 1.00f);
            colors[ImGuiCol_ResizeGrip]           = ImVec4(0.063f, 0.251f, 0.161f, 0.40f);
            colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.102f, 0.380f, 0.243f, 0.75f);
            colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.145f, 0.502f, 0.318f, 1.00f);
            colors[ImGuiCol_Tab]                  = colors[ImGuiCol_Button];
            colors[ImGuiCol_TabHovered]           = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_TabSelected]          = colors[ImGuiCol_ButtonActive];
            colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.071f, 0.051f, 0.027f, 1.00f);
            colors[ImGuiCol_TableBorderStrong]    = ImVec4(0.255f, 0.196f, 0.133f, 1.00f);
            colors[ImGuiCol_TableBorderLight]     = ImVec4(0.157f, 0.118f, 0.071f, 1.00f);
            colors[ImGuiCol_TableRowBg]           = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt]        = ImVec4(1.000f, 1.000f, 1.000f, 0.03f);
            colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.188f, 0.729f, 0.471f, 0.35f);
            break;
        }
        // ── Material Light — Material Design 3; Roboto + Hack ─────────────────
        // bg: #FAFAFE  primary: #6750A4 (M3 purple)  outline: #79747E
        case ThemeType::Material: {
            colors[ImGuiCol_Text]                 = ImVec4(0.110f, 0.106f, 0.122f, 1.00f);
            colors[ImGuiCol_TextDisabled]         = ImVec4(0.475f, 0.455f, 0.494f, 1.00f);
            colors[ImGuiCol_WindowBg]             = ImVec4(0.980f, 0.969f, 0.996f, 1.00f);
            colors[ImGuiCol_ChildBg]              = ImVec4(0.953f, 0.929f, 0.969f, 1.00f);
            colors[ImGuiCol_PopupBg]              = ImVec4(0.984f, 0.976f, 0.996f, 0.98f);
            colors[ImGuiCol_Border]               = ImVec4(0.741f, 0.718f, 0.757f, 1.00f);
            colors[ImGuiCol_BorderShadow]         = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_FrameBg]              = ImVec4(0.922f, 0.898f, 0.937f, 1.00f);
            colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.878f, 0.847f, 0.914f, 1.00f);
            colors[ImGuiCol_FrameBgActive]        = ImVec4(0.831f, 0.796f, 0.882f, 1.00f);
            colors[ImGuiCol_TitleBg]              = ImVec4(0.953f, 0.929f, 0.969f, 1.00f);
            colors[ImGuiCol_TitleBgActive]        = ImVec4(0.918f, 0.867f, 1.000f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.973f, 0.961f, 0.984f, 1.00f);
            colors[ImGuiCol_MenuBarBg]            = ImVec4(0.953f, 0.929f, 0.969f, 1.00f);
            colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.953f, 0.929f, 0.969f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.741f, 0.718f, 0.757f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.600f, 0.573f, 0.616f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.475f, 0.455f, 0.494f, 1.00f);
            colors[ImGuiCol_CheckMark]            = ImVec4(0.404f, 0.314f, 0.643f, 1.00f);
            colors[ImGuiCol_SliderGrab]           = ImVec4(0.404f, 0.314f, 0.643f, 1.00f);
            colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.318f, 0.239f, 0.529f, 1.00f);
            colors[ImGuiCol_Button]               = ImVec4(0.404f, 0.314f, 0.643f, 1.00f);
            colors[ImGuiCol_ButtonHovered]        = ImVec4(0.478f, 0.392f, 0.714f, 1.00f);
            colors[ImGuiCol_ButtonActive]         = ImVec4(0.545f, 0.459f, 0.780f, 1.00f);
            colors[ImGuiCol_Header]               = ImVec4(0.867f, 0.835f, 0.941f, 1.00f);
            colors[ImGuiCol_HeaderHovered]        = ImVec4(0.404f, 0.314f, 0.643f, 0.60f);
            colors[ImGuiCol_HeaderActive]         = ImVec4(0.404f, 0.314f, 0.643f, 0.90f);
            colors[ImGuiCol_Separator]            = ImVec4(0.741f, 0.718f, 0.757f, 1.00f);
            colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.404f, 0.314f, 0.643f, 0.80f);
            colors[ImGuiCol_SeparatorActive]      = ImVec4(0.404f, 0.314f, 0.643f, 1.00f);
            colors[ImGuiCol_ResizeGrip]           = ImVec4(0.404f, 0.314f, 0.643f, 0.35f);
            colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.404f, 0.314f, 0.643f, 0.65f);
            colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.404f, 0.314f, 0.643f, 0.95f);
            colors[ImGuiCol_Tab]                  = colors[ImGuiCol_Header];
            colors[ImGuiCol_TabHovered]           = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_TabSelected]          = colors[ImGuiCol_Button];
            colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.918f, 0.867f, 1.000f, 1.00f);
            colors[ImGuiCol_TableBorderStrong]    = ImVec4(0.741f, 0.718f, 0.757f, 1.00f);
            colors[ImGuiCol_TableBorderLight]     = ImVec4(0.839f, 0.824f, 0.859f, 1.00f);
            colors[ImGuiCol_TableRowBg]           = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt]        = ImVec4(0.000f, 0.000f, 0.000f, 0.03f);
            colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.404f, 0.314f, 0.643f, 0.30f);
            break;
        }
        // ── MaterialDark — Material Design 3 dark; Roboto + Hack ──────────────
        // bg: #1C1B1F  primary (on-dark): #D0BCFF  container: #4F378B
        case ThemeType::MaterialDark: {
            colors[ImGuiCol_Text]                 = ImVec4(0.902f, 0.882f, 0.898f, 1.00f);
            colors[ImGuiCol_TextDisabled]         = ImVec4(0.576f, 0.561f, 0.600f, 1.00f);
            colors[ImGuiCol_WindowBg]             = ImVec4(0.110f, 0.106f, 0.122f, 1.00f);
            colors[ImGuiCol_ChildBg]              = ImVec4(0.145f, 0.141f, 0.161f, 1.00f);
            colors[ImGuiCol_PopupBg]              = ImVec4(0.090f, 0.086f, 0.098f, 0.98f);
            colors[ImGuiCol_Border]               = ImVec4(0.351f, 0.341f, 0.369f, 1.00f);
            colors[ImGuiCol_BorderShadow]         = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_FrameBg]              = ImVec4(0.188f, 0.184f, 0.204f, 1.00f);
            colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.239f, 0.235f, 0.259f, 1.00f);
            colors[ImGuiCol_FrameBgActive]        = ImVec4(0.290f, 0.282f, 0.314f, 1.00f);
            colors[ImGuiCol_TitleBg]              = ImVec4(0.090f, 0.086f, 0.098f, 1.00f);
            colors[ImGuiCol_TitleBgActive]        = ImVec4(0.145f, 0.141f, 0.161f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.071f, 0.067f, 0.078f, 1.00f);
            colors[ImGuiCol_MenuBarBg]            = ImVec4(0.110f, 0.106f, 0.122f, 1.00f);
            colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.090f, 0.086f, 0.098f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.271f, 0.263f, 0.294f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.351f, 0.341f, 0.380f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.431f, 0.420f, 0.463f, 1.00f);
            colors[ImGuiCol_CheckMark]            = ImVec4(0.816f, 0.737f, 1.000f, 1.00f); // #D0BCFF
            colors[ImGuiCol_SliderGrab]           = ImVec4(0.580f, 0.486f, 0.800f, 1.00f);
            colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.816f, 0.737f, 1.000f, 1.00f);
            colors[ImGuiCol_Button]               = ImVec4(0.310f, 0.216f, 0.545f, 1.00f); // #4F378B
            colors[ImGuiCol_ButtonHovered]        = ImVec4(0.404f, 0.306f, 0.639f, 1.00f);
            colors[ImGuiCol_ButtonActive]         = ImVec4(0.486f, 0.392f, 0.722f, 1.00f);
            colors[ImGuiCol_Header]               = ImVec4(0.247f, 0.235f, 0.302f, 1.00f);
            colors[ImGuiCol_HeaderHovered]        = ImVec4(0.310f, 0.216f, 0.545f, 0.80f);
            colors[ImGuiCol_HeaderActive]         = ImVec4(0.404f, 0.306f, 0.639f, 1.00f);
            colors[ImGuiCol_Separator]            = ImVec4(0.351f, 0.341f, 0.369f, 1.00f);
            colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.816f, 0.737f, 1.000f, 0.70f);
            colors[ImGuiCol_SeparatorActive]      = ImVec4(0.816f, 0.737f, 1.000f, 1.00f);
            colors[ImGuiCol_ResizeGrip]           = ImVec4(0.310f, 0.216f, 0.545f, 0.40f);
            colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.404f, 0.306f, 0.639f, 0.75f);
            colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.486f, 0.392f, 0.722f, 1.00f);
            colors[ImGuiCol_Tab]                  = colors[ImGuiCol_Button];
            colors[ImGuiCol_TabHovered]           = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_TabSelected]          = colors[ImGuiCol_ButtonActive];
            colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.169f, 0.165f, 0.184f, 1.00f);
            colors[ImGuiCol_TableBorderStrong]    = ImVec4(0.351f, 0.341f, 0.369f, 1.00f);
            colors[ImGuiCol_TableBorderLight]     = ImVec4(0.247f, 0.243f, 0.267f, 1.00f);
            colors[ImGuiCol_TableRowBg]           = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt]        = ImVec4(1.000f, 1.000f, 1.000f, 0.03f);
            colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.816f, 0.737f, 1.000f, 0.30f);
            break;
        }
        // ── MonoLight — minimal monochrome light; JetBrainsMono + JetBrainsMono
        // bg: near-white #F8F8F8  accent: slate blue #4A6FA5
        case ThemeType::MonoLight: {
            colors[ImGuiCol_Text]                 = ImVec4(0.102f, 0.102f, 0.102f, 1.00f);
            colors[ImGuiCol_TextDisabled]         = ImVec4(0.600f, 0.600f, 0.600f, 1.00f);
            colors[ImGuiCol_WindowBg]             = ImVec4(0.973f, 0.973f, 0.973f, 1.00f);
            colors[ImGuiCol_ChildBg]              = ImVec4(0.941f, 0.941f, 0.941f, 1.00f);
            colors[ImGuiCol_PopupBg]              = ImVec4(0.988f, 0.988f, 0.988f, 0.98f);
            colors[ImGuiCol_Border]               = ImVec4(0.800f, 0.800f, 0.800f, 1.00f);
            colors[ImGuiCol_BorderShadow]         = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_FrameBg]              = ImVec4(0.918f, 0.918f, 0.918f, 1.00f);
            colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.878f, 0.878f, 0.878f, 1.00f);
            colors[ImGuiCol_FrameBgActive]        = ImVec4(0.831f, 0.831f, 0.831f, 1.00f);
            colors[ImGuiCol_TitleBg]              = ImVec4(0.929f, 0.929f, 0.929f, 1.00f);
            colors[ImGuiCol_TitleBgActive]        = ImVec4(0.898f, 0.906f, 0.922f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.953f, 0.953f, 0.953f, 1.00f);
            colors[ImGuiCol_MenuBarBg]            = ImVec4(0.929f, 0.929f, 0.929f, 1.00f);
            colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.941f, 0.941f, 0.941f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.749f, 0.749f, 0.749f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.651f, 0.651f, 0.651f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.549f, 0.549f, 0.549f, 1.00f);
            colors[ImGuiCol_CheckMark]            = ImVec4(0.290f, 0.435f, 0.647f, 1.00f);
            colors[ImGuiCol_SliderGrab]           = ImVec4(0.290f, 0.435f, 0.647f, 1.00f);
            colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.212f, 0.345f, 0.549f, 1.00f);
            colors[ImGuiCol_Button]               = ImVec4(0.290f, 0.435f, 0.647f, 1.00f);
            colors[ImGuiCol_ButtonHovered]        = ImVec4(0.365f, 0.514f, 0.718f, 1.00f);
            colors[ImGuiCol_ButtonActive]         = ImVec4(0.435f, 0.580f, 0.788f, 1.00f);
            colors[ImGuiCol_Header]               = ImVec4(0.800f, 0.820f, 0.859f, 1.00f);
            colors[ImGuiCol_HeaderHovered]        = ImVec4(0.290f, 0.435f, 0.647f, 0.60f);
            colors[ImGuiCol_HeaderActive]         = ImVec4(0.290f, 0.435f, 0.647f, 0.90f);
            colors[ImGuiCol_Separator]            = ImVec4(0.800f, 0.800f, 0.800f, 1.00f);
            colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.290f, 0.435f, 0.647f, 0.70f);
            colors[ImGuiCol_SeparatorActive]      = ImVec4(0.290f, 0.435f, 0.647f, 1.00f);
            colors[ImGuiCol_ResizeGrip]           = ImVec4(0.290f, 0.435f, 0.647f, 0.35f);
            colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.290f, 0.435f, 0.647f, 0.65f);
            colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.290f, 0.435f, 0.647f, 0.95f);
            colors[ImGuiCol_Tab]                  = colors[ImGuiCol_Header];
            colors[ImGuiCol_TabHovered]           = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_TabSelected]          = colors[ImGuiCol_Button];
            colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.890f, 0.898f, 0.918f, 1.00f);
            colors[ImGuiCol_TableBorderStrong]    = ImVec4(0.800f, 0.800f, 0.800f, 1.00f);
            colors[ImGuiCol_TableBorderLight]     = ImVec4(0.859f, 0.859f, 0.859f, 1.00f);
            colors[ImGuiCol_TableRowBg]           = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt]        = ImVec4(0.000f, 0.000f, 0.000f, 0.03f);
            colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.290f, 0.435f, 0.647f, 0.30f);
            break;
        }
        // ── MonoDark — minimal monochrome dark; JetBrainsMono + JetBrainsMono ──
        // bg: near-black #1A1A1A  accent: slate blue #5B7FBB
        case ThemeType::MonoDark: {
            colors[ImGuiCol_Text]                 = ImVec4(0.894f, 0.894f, 0.894f, 1.00f);
            colors[ImGuiCol_TextDisabled]         = ImVec4(0.502f, 0.502f, 0.502f, 1.00f);
            colors[ImGuiCol_WindowBg]             = ImVec4(0.102f, 0.102f, 0.102f, 1.00f);
            colors[ImGuiCol_ChildBg]              = ImVec4(0.129f, 0.129f, 0.129f, 1.00f);
            colors[ImGuiCol_PopupBg]              = ImVec4(0.086f, 0.086f, 0.086f, 0.98f);
            colors[ImGuiCol_Border]               = ImVec4(0.220f, 0.220f, 0.220f, 1.00f);
            colors[ImGuiCol_BorderShadow]         = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_FrameBg]              = ImVec4(0.169f, 0.169f, 0.169f, 1.00f);
            colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.208f, 0.208f, 0.208f, 1.00f);
            colors[ImGuiCol_FrameBgActive]        = ImVec4(0.255f, 0.255f, 0.255f, 1.00f);
            colors[ImGuiCol_TitleBg]              = ImVec4(0.086f, 0.086f, 0.086f, 1.00f);
            colors[ImGuiCol_TitleBgActive]        = ImVec4(0.129f, 0.141f, 0.165f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.067f, 0.067f, 0.067f, 1.00f);
            colors[ImGuiCol_MenuBarBg]            = ImVec4(0.110f, 0.110f, 0.110f, 1.00f);
            colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.086f, 0.086f, 0.086f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.275f, 0.275f, 0.275f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.357f, 0.357f, 0.357f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.447f, 0.447f, 0.447f, 1.00f);
            colors[ImGuiCol_CheckMark]            = ImVec4(0.357f, 0.498f, 0.733f, 1.00f);
            colors[ImGuiCol_SliderGrab]           = ImVec4(0.227f, 0.349f, 0.533f, 1.00f);
            colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.357f, 0.498f, 0.733f, 1.00f);
            colors[ImGuiCol_Button]               = ImVec4(0.196f, 0.306f, 0.478f, 1.00f);
            colors[ImGuiCol_ButtonHovered]        = ImVec4(0.263f, 0.388f, 0.573f, 1.00f);
            colors[ImGuiCol_ButtonActive]         = ImVec4(0.357f, 0.498f, 0.733f, 1.00f);
            colors[ImGuiCol_Header]               = ImVec4(0.188f, 0.220f, 0.302f, 1.00f);
            colors[ImGuiCol_HeaderHovered]        = ImVec4(0.227f, 0.290f, 0.420f, 1.00f);
            colors[ImGuiCol_HeaderActive]         = ImVec4(0.290f, 0.380f, 0.541f, 1.00f);
            colors[ImGuiCol_Separator]            = ImVec4(0.220f, 0.220f, 0.220f, 1.00f);
            colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.357f, 0.498f, 0.733f, 0.70f);
            colors[ImGuiCol_SeparatorActive]      = ImVec4(0.357f, 0.498f, 0.733f, 1.00f);
            colors[ImGuiCol_ResizeGrip]           = ImVec4(0.196f, 0.306f, 0.478f, 0.40f);
            colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.263f, 0.388f, 0.573f, 0.75f);
            colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.357f, 0.498f, 0.733f, 1.00f);
            colors[ImGuiCol_Tab]                  = colors[ImGuiCol_Button];
            colors[ImGuiCol_TabHovered]           = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_TabSelected]          = colors[ImGuiCol_ButtonActive];
            colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.153f, 0.157f, 0.176f, 1.00f);
            colors[ImGuiCol_TableBorderStrong]    = ImVec4(0.220f, 0.220f, 0.220f, 1.00f);
            colors[ImGuiCol_TableBorderLight]     = ImVec4(0.169f, 0.169f, 0.169f, 1.00f);
            colors[ImGuiCol_TableRowBg]           = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt]        = ImVec4(1.000f, 1.000f, 1.000f, 0.03f);
            colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.357f, 0.498f, 0.733f, 0.35f);
            break;
        }
        // ── DawnBringerLight — DB16 light palette + Monaspace Argon ───────────
        // Same colors as DawnBringer16Light; code font: MonaspaceArgonNF.
        case ThemeType::DawnBringerLight: {
            colors[ImGuiCol_Text]                 = ImVec4(0.078f, 0.047f, 0.110f, 1.00f);
            colors[ImGuiCol_TextDisabled]         = ImVec4(0.459f, 0.443f, 0.380f, 1.00f);
            colors[ImGuiCol_WindowBg]             = ImVec4(0.871f, 0.933f, 0.839f, 1.00f);
            colors[ImGuiCol_ChildBg]              = ImVec4(0.835f, 0.898f, 0.808f, 1.00f);
            colors[ImGuiCol_PopupBg]              = ImVec4(0.890f, 0.945f, 0.859f, 0.98f);
            colors[ImGuiCol_Border]               = ImVec4(0.522f, 0.584f, 0.631f, 1.00f);
            colors[ImGuiCol_BorderShadow]         = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_FrameBg]              = ImVec4(0.824f, 0.667f, 0.600f, 1.00f);
            colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.710f, 0.745f, 0.855f, 1.00f);
            colors[ImGuiCol_FrameBgActive]        = ImVec4(0.349f, 0.490f, 0.808f, 0.70f);
            colors[ImGuiCol_TitleBg]              = ImVec4(0.820f, 0.882f, 0.800f, 1.00f);
            colors[ImGuiCol_TitleBgActive]        = ImVec4(0.780f, 0.843f, 0.757f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.855f, 0.914f, 0.827f, 1.00f);
            colors[ImGuiCol_MenuBarBg]            = ImVec4(0.820f, 0.882f, 0.800f, 1.00f);
            colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.835f, 0.898f, 0.808f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.522f, 0.584f, 0.631f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.427f, 0.667f, 0.173f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.204f, 0.396f, 0.141f, 1.00f);
            colors[ImGuiCol_CheckMark]            = ImVec4(0.204f, 0.396f, 0.141f, 1.00f);
            colors[ImGuiCol_SliderGrab]           = ImVec4(0.349f, 0.490f, 0.808f, 1.00f);
            colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.204f, 0.396f, 0.141f, 1.00f);
            colors[ImGuiCol_Button]               = ImVec4(0.204f, 0.396f, 0.141f, 1.00f);
            colors[ImGuiCol_ButtonHovered]        = ImVec4(0.310f, 0.537f, 0.224f, 1.00f);
            colors[ImGuiCol_ButtonActive]         = ImVec4(0.427f, 0.667f, 0.173f, 1.00f);
            colors[ImGuiCol_Header]               = ImVec4(0.710f, 0.767f, 0.855f, 1.00f);
            colors[ImGuiCol_HeaderHovered]        = ImVec4(0.349f, 0.490f, 0.808f, 0.70f);
            colors[ImGuiCol_HeaderActive]         = ImVec4(0.188f, 0.310f, 0.620f, 1.00f);
            colors[ImGuiCol_Separator]            = ImVec4(0.522f, 0.584f, 0.631f, 1.00f);
            colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.204f, 0.396f, 0.141f, 0.80f);
            colors[ImGuiCol_SeparatorActive]      = ImVec4(0.204f, 0.396f, 0.141f, 1.00f);
            colors[ImGuiCol_ResizeGrip]           = ImVec4(0.204f, 0.396f, 0.141f, 0.35f);
            colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.310f, 0.537f, 0.224f, 0.70f);
            colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.427f, 0.667f, 0.173f, 1.00f);
            colors[ImGuiCol_Tab]                  = ImVec4(0.824f, 0.710f, 0.655f, 1.00f);
            colors[ImGuiCol_TabHovered]           = ImVec4(0.349f, 0.490f, 0.808f, 0.70f);
            colors[ImGuiCol_TabSelected]          = ImVec4(0.204f, 0.396f, 0.141f, 1.00f);
            colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.824f, 0.667f, 0.600f, 1.00f);
            colors[ImGuiCol_TableBorderStrong]    = ImVec4(0.522f, 0.584f, 0.631f, 1.00f);
            colors[ImGuiCol_TableBorderLight]     = ImVec4(0.700f, 0.757f, 0.700f, 1.00f);
            colors[ImGuiCol_TableRowBg]           = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt]        = ImVec4(0.000f, 0.000f, 0.000f, 0.04f);
            colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.427f, 0.667f, 0.173f, 0.38f);
            break;
        }
        // ── DawnBringerDark — DB16 dark palette + Monaspace Argon ────────────
        // Same colors as DawnBringer16Dark; code font: MonaspaceArgonNF.
        case ThemeType::DawnBringerDark: {
            colors[ImGuiCol_Text]                 = ImVec4(0.871f, 0.933f, 0.839f, 1.00f);
            colors[ImGuiCol_TextDisabled]         = ImVec4(0.459f, 0.443f, 0.380f, 1.00f);
            colors[ImGuiCol_WindowBg]             = ImVec4(0.078f, 0.047f, 0.110f, 1.00f);
            colors[ImGuiCol_ChildBg]              = ImVec4(0.098f, 0.063f, 0.137f, 1.00f);
            colors[ImGuiCol_PopupBg]              = ImVec4(0.059f, 0.035f, 0.090f, 0.98f);
            colors[ImGuiCol_Border]               = ImVec4(0.306f, 0.290f, 0.306f, 1.00f);
            colors[ImGuiCol_BorderShadow]         = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_FrameBg]              = ImVec4(0.267f, 0.141f, 0.204f, 1.00f);
            colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.188f, 0.204f, 0.427f, 1.00f);
            colors[ImGuiCol_FrameBgActive]        = ImVec4(0.349f, 0.490f, 0.808f, 1.00f);
            colors[ImGuiCol_TitleBg]              = ImVec4(0.090f, 0.055f, 0.129f, 1.00f);
            colors[ImGuiCol_TitleBgActive]        = ImVec4(0.267f, 0.141f, 0.204f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.059f, 0.035f, 0.086f, 1.00f);
            colors[ImGuiCol_MenuBarBg]            = ImVec4(0.090f, 0.055f, 0.129f, 1.00f);
            colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.078f, 0.047f, 0.110f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.306f, 0.290f, 0.306f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.427f, 0.761f, 0.792f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.349f, 0.490f, 0.808f, 1.00f);
            colors[ImGuiCol_CheckMark]            = ImVec4(0.427f, 0.761f, 0.792f, 1.00f);
            colors[ImGuiCol_SliderGrab]           = ImVec4(0.188f, 0.204f, 0.427f, 1.00f);
            colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.427f, 0.761f, 0.792f, 1.00f);
            colors[ImGuiCol_Button]               = ImVec4(0.188f, 0.204f, 0.427f, 1.00f);
            colors[ImGuiCol_ButtonHovered]        = ImVec4(0.280f, 0.310f, 0.580f, 1.00f);
            colors[ImGuiCol_ButtonActive]         = ImVec4(0.427f, 0.761f, 0.792f, 1.00f);
            colors[ImGuiCol_Header]               = ImVec4(0.267f, 0.141f, 0.204f, 1.00f);
            colors[ImGuiCol_HeaderHovered]        = ImVec4(0.188f, 0.204f, 0.427f, 1.00f);
            colors[ImGuiCol_HeaderActive]         = ImVec4(0.349f, 0.490f, 0.808f, 1.00f);
            colors[ImGuiCol_Separator]            = ImVec4(0.306f, 0.290f, 0.306f, 1.00f);
            colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.427f, 0.761f, 0.792f, 0.80f);
            colors[ImGuiCol_SeparatorActive]      = ImVec4(0.427f, 0.761f, 0.792f, 1.00f);
            colors[ImGuiCol_ResizeGrip]           = ImVec4(0.188f, 0.204f, 0.427f, 0.40f);
            colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.280f, 0.310f, 0.580f, 0.75f);
            colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.427f, 0.761f, 0.792f, 1.00f);
            colors[ImGuiCol_Tab]                  = colors[ImGuiCol_Button];
            colors[ImGuiCol_TabHovered]           = colors[ImGuiCol_ButtonHovered];
            colors[ImGuiCol_TabSelected]          = colors[ImGuiCol_ButtonActive];
            colors[ImGuiCol_TableHeaderBg]        = ImVec4(0.267f, 0.141f, 0.204f, 1.00f);
            colors[ImGuiCol_TableBorderStrong]    = ImVec4(0.306f, 0.290f, 0.306f, 1.00f);
            colors[ImGuiCol_TableBorderLight]     = ImVec4(0.196f, 0.184f, 0.200f, 1.00f);
            colors[ImGuiCol_TableRowBg]           = ImVec4(0.000f, 0.000f, 0.000f, 0.00f);
            colors[ImGuiCol_TableRowBgAlt]        = ImVec4(1.000f, 1.000f, 1.000f, 0.04f);
            colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.427f, 0.761f, 0.792f, 0.38f);
            break;
        }
    } // switch

    // ── Switch UI and code fonts, store current theme, apply style ────────────
    const auto i = static_cast<std::size_t>(theme);
    if (uiFonts_[i])   ImGui::GetIO().FontDefault = uiFonts_[i];
    if (codeFonts_[i]) codeFont                   = codeFonts_[i];
    current_ = theme;
    ApplyThemeStyle_(theme);
}
