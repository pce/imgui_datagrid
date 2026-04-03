#pragma once
#include <cstdlib>
#include <string>

#include "../../../adapters/platform_detect.hpp"

namespace UI::Platform {

// Re-export from the shared detection header so UI code uses UI::Platform::* names.
inline constexpr bool isMacOS   = ::Platform::isMacOS;
inline constexpr bool isWindows = ::Platform::isWindows;
inline constexpr bool isLinux   = ::Platform::isLinux;

/// Primary modifier key name for display in menu shortcuts.
inline constexpr const char* kModName = isMacOS ? "Cmd" : "Ctrl";

/// Single-click navigates on macOS (Finder convention);
/// double-click navigates on Windows/Linux (Explorer convention).
inline constexpr bool kClickNavigates    = isMacOS;
inline constexpr bool kDblClickNavigates = !isMacOS;

inline void OpenWithSystem(const std::string& path)
{
    if constexpr (isMacOS) {
        (void)std::system(("open \"" + path + "\"").c_str());
    } else if constexpr (isWindows) {
        (void)std::system(("start \"\" \"" + path + "\"").c_str());
    } else {
        (void)std::system(("xdg-open \"" + path + "\"").c_str());
    }
}

} // namespace UI::Platform