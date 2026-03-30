#pragma once

#include "imgui.h"
#include "theme.hpp"

#include <array>

class Navbar {
public:
    bool wantsQuit       = false;
    bool wantsFullscreen = false;
    bool wantsOpen       = false;

    void Render(Theme& theme)
    {
        if (!ImGui::BeginMainMenuBar()) return;

        if (ImGui::BeginMenu("File")) {
#if defined(__APPLE__)
            constexpr const char* kOpenShortcut = "Cmd+O";
            constexpr const char* kQuitShortcut = "Cmd+Q";
#else
            constexpr const char* kOpenShortcut = "Ctrl+O";
            constexpr const char* kQuitShortcut = "Ctrl+Q";
#endif
            if (ImGui::MenuItem("Open Data Source…", kOpenShortcut))
                wantsOpen = true;

            ImGui::Separator();

            if (ImGui::MenuItem("Fullscreen", "F11"))
                wantsFullscreen = true;

            ImGui::Separator();

            if (ImGui::MenuItem("Quit", kQuitShortcut))
                wantsQuit = true;

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Theme")) {
            static constexpr std::array<const char*, 3> kLabels = {
                "Solarized Dark", "Solarized Light", "Monokai"
            };
            for (int i = 0; i < static_cast<int>(kLabels.size()); ++i) {
                if (ImGui::MenuItem(kLabels[i], nullptr, currentThemeIndex_ == i)) {
                    currentThemeIndex_ = i;
                    theme.ApplyColorTheme(static_cast<ThemeType>(i));
                }
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

private:
    int currentThemeIndex_ = 0;
};
