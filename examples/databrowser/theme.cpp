#include "theme.hpp"

void Theme::ApplyImGuiStyle(float dpiScale)
{
    const float s = (dpiScale > 0.0f) ? dpiScale : 1.0f;

    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding    = 4.0f  * s;
    style.FrameRounding     = 4.0f  * s;
    style.ChildRounding     = 4.0f  * s;
    style.PopupRounding     = 4.0f  * s;
    style.TabRounding       = 4.0f  * s;
    style.ScrollbarRounding = 4.0f  * s;
    style.GrabRounding      = 4.0f  * s;
    style.ScrollbarSize     = 14.0f * s;
    style.GrabMinSize       = 10.0f * s;

    style.WindowPadding     = ImVec2(10.0f * s,  10.0f * s);
    style.FramePadding      = ImVec2( 6.0f * s,   4.0f * s);
    style.ItemSpacing       = ImVec2( 8.0f * s,   4.0f * s);
    style.ItemInnerSpacing  = ImVec2( 4.0f * s,   4.0f * s);
    style.CellPadding       = ImVec2( 4.0f * s,   2.0f * s);
    style.IndentSpacing     = 21.0f * s;
}

void Theme::ApplyColorTheme(ThemeType theme)
{
    ImVec4* colors = ImGui::GetStyle().Colors;

    switch (theme) {
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
        break;
    }
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
        break;
    }
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
        break;
    }
    }
}
