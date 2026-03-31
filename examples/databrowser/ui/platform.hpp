#pragma once
#include <cstdlib>
#include <string>


namespace UI::Platform {

inline constexpr bool isMacOS =
#if defined(__APPLE__)
    true;
#else
    false;
#endif

inline constexpr bool isWindows =
#if defined(_WIN32)
    true;
#else
    false;
#endif

inline constexpr bool isLinux =
#if defined(__linux__)
    true;
#else
    false;
#endif

/// Primary modifier key name for display in menu shortcuts.
inline constexpr const char* kModName =
#if defined(__APPLE__)
    "Cmd";
#else
    "Ctrl";
#endif

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