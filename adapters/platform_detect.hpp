#pragma once
#include <ctime>
#include <string_view>

/// Compile-time OS detection — usable across adapter and UI layers.
/// Prefer `if constexpr` over raw `#ifdef` for branching logic so that
/// all branches are syntax-checked by the compiler on every platform.
///
/// Preprocessor `#if` is still needed for platform-specific *includes*
/// (e.g. <windows.h> vs <unistd.h>); these should be isolated at the
/// top of each translation unit and not scattered through function bodies.

namespace Platform {

inline constexpr bool isWindows =
#if defined(_WIN32)
    true;
#else
    false;
#endif

inline constexpr bool isMacOS =
#if defined(__APPLE__)
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

inline constexpr bool isPosix = !isWindows;

/// Display name for the file-permission/attribute column.
/// "permissions" on POSIX (full rwxr-xr-x), "attributes" on Windows (r-- / rw-).
inline constexpr std::string_view kPermColName = isPosix ? "permissions" : "attributes";

/// Thread-safe local time conversion — wraps localtime_s (Windows) / localtime_r (POSIX).
/// Returns nullptr on failure.
inline std::tm* localtime_safe(const std::time_t* tt, std::tm* tmBuf) noexcept
{
#if defined(_WIN32)
    return localtime_s(tmBuf, tt) == 0 ? tmBuf : nullptr;
#else
    return ::localtime_r(tt, tmBuf);
#endif
}

} // namespace Platform
