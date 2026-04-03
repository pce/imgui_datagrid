#pragma once

#include "imgui.h"
#include "theme.hpp"

#include <array>
#include <functional>
#include <string>
#include <vector>


namespace datagrid::ui {
    /// Compact INI blob restoring the default two-panel layout.
    /// Replace with a real captured ini string if a fixed default layout is needed.
    inline constexpr const char* kDefaultLayoutIni = "";
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
        bool wantsQuit          = false;
        bool wantsFullscreen    = false;
        bool wantsOpen          = false;
        bool wantsThemeCustomizer = false; ///< Toggled by Settings → "Theme Customizer…"

        /// Called after a theme switch; receives the newly selected ThemeStyle.
        /// Use this to apply palette colors and propagate the code-font pointer.
        std::function<void(ThemeStyle)> onThemeApplied;

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


            if (!windows_.empty() && ImGui::BeginMenu("Window")) {
                for (const auto& w : windows_) {
                    if (ImGui::MenuItem(w.label.c_str(), nullptr, w.focused)) {
                        if (w.onSelect)
                            w.onSelect();
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Settings")) {
                if (ImGui::MenuItem("Reset layout")) {
                    ImGui::LoadIniSettingsFromMemory(kDefaultLayoutIni);
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Theme Customizer\xe2\x80\xa6")) {
                    wantsThemeCustomizer = true;
                }
                ImGui::EndMenu();
            }

            ImGui::EndMainMenuBar();
        }

    private:
        int                      currentThemeIndex_ = 0;
        std::vector<WindowEntry> windows_;
    };
} // namespace datagrid::ui
