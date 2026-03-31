#pragma once

#include "imgui.h"
#include "../theme.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>


namespace UI {
    /// Describes one entry in the Navbar "Window" submenu.
    /// Built each frame by the App and passed to Navbar::SetWindows().
    struct WindowEntry
    {
        std::string           label;           ///< Menu item display text
        bool                  focused = false; ///< Show checkmark when true
        std::function<void()> onSelect;        ///< Called when the item is clicked
    };

    class Navbar
    {
    public:
        bool wantsQuit       = false;
        bool wantsFullscreen = false;
        bool wantsOpen       = false;

        /// Called after a theme switch (colors + fonts already applied).
        /// Use this to propagate the new code-font pointer to other widgets.
        std::function<void()> onThemeApplied;

        void SetWindows(std::vector<WindowEntry> entries) { windows_ = std::move(entries); }

        void Render(Theme& theme)
        {
            if (!ImGui::BeginMainMenuBar())
                return;

            if (ImGui::BeginMenu("File")) {
#if defined(__APPLE__)
                constexpr const char* kOpenShortcut = "Cmd+O";
                constexpr const char* kQuitShortcut = "Cmd+Q";
#else
                constexpr const char* kOpenShortcut = "Ctrl+O";
                constexpr const char* kQuitShortcut = "Ctrl+Q";
#endif
                if (ImGui::MenuItem("Open Data Source\xe2\x80\xa6", kOpenShortcut))
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
                static constexpr std::array<const char*, kThemeCount> kLabels = {
                    "Solarized Dark",
                    "Solarized Light",
                    "Monokai",
                    "Monokai Dark (Pro)",
                    "Mona Spaces (Floral)",
                    "Earth / SUSE",
                    "Earth / SUSE Dark",
                    "Neon Spaces",
                    "DawnBringer 16 \xe2\x80\x94 Dark",
                    "DawnBringer 16 \xe2\x80\x94 Light",
                    "Material",
                    "Material Dark",
                    "Mono Light",
                    "Mono Dark",
                    "DawnBringer \xe2\x80\x94 Light (Argon)",
                    "DawnBringer \xe2\x80\x94 Dark (Argon)",
                };
                for (int i = 0; i < static_cast<int>(kLabels.size()); ++i) {
                    if (ImGui::MenuItem(kLabels[i], nullptr, currentThemeIndex_ == i)) {
                        currentThemeIndex_ = i;
                        theme.ApplyColorTheme(static_cast<ThemeType>(i));
                        if (onThemeApplied) onThemeApplied();
                    }
                }
                ImGui::EndMenu();
            }

            if (!windows_.empty() && ImGui::BeginMenu("Window")) {
                for (const auto& w : windows_) {
                    if (ImGui::MenuItem(w.label.c_str(), nullptr, w.focused)) {
                        if (w.onSelect)
                            w.onSelect();
                    }
                }
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

    private:
        int                      currentThemeIndex_ = 0;
        std::vector<WindowEntry> windows_;
    };
}